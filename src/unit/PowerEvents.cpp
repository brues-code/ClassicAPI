// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `UNIT_POWER_UPDATE`, `UNIT_POWER_FREQUENT` and `UNIT_MAXPOWER` — the
// generic power events, `(unitTarget, powerType)`, beside 1.12's per-type
// UNIT_MANA / UNIT_RAGE / … and UNIT_MAXMANA / ….
//
// 4.3.4 fires them from two type-wide mirror handlers its unit class
// registers (Unit_C.cpp): UnitPowerUpdateHandler on each power slot signals
// UNIT_POWER (retail's UNIT_POWER_UPDATE) and UNIT_POWER_FREQUENT, and
// UnitMaxPowerUpdateHandler on each max-power slot signals UNIT_MAXPOWER,
// each through Script_SendUnitSignal with the power-type token, so once per
// token of the unit.
//
// 1.12 has the same type-wide observers: CGUnit_C::RegisterFieldWatchers
// registers its unit handlers with FUN_TYPE_OBSERVER_REGISTER, and its own
// OnPowerFieldChanged watches the power block (it only refreshes usability).
// So the two handlers join that list, one record per slot so the callback's
// field offset names the power type (the slot is the type in 1.12). Every
// in-world registration pass removes and re-adds them: the first pass of a
// session runs in CGGameUI::InitializeGame, after MirrorInitialize has built
// the tables the record's copy offset comes from, and the removal keeps
// `/reload` from stacking a second record. 1.12 predicts no power, so every
// change is a server update and UNIT_POWER_FREQUENT fires with
// UNIT_POWER_UPDATE, as 4.3.4's handler does on a server update.
//
// 4.3.4 also fires them from its party-record dispatcher for a group member's
// stats packet; Group::MemberEvents does that through SignalPower /
// SignalMaxPower.

#include "unit/PowerEvents.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "unit/Identity.h"
#include "unit/Power.h"

#include <cstdint>

namespace Unit::PowerEvents {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("powerType", "string"),
};
const Game::Doc::Event kUpdateDoc{"Unit", "Fires when the unit's power changes.", kPayload};
const Game::Doc::Event kFrequentDoc{"Unit", "Fires on every change of the unit's power.",
                                    kPayload};
const Game::Doc::Event kMaxDoc{"Unit", "Fires when the unit's maximum power changes.", kPayload};
const Event::Custom::AutoReserve kUpdate{"UNIT_POWER_UPDATE", &kUpdateDoc};
const Event::Custom::AutoReserve kFrequent{"UNIT_POWER_FREQUENT", &kFrequentDoc};
const Event::Custom::AutoReserve kMax{"UNIT_MAXPOWER", &kMaxDoc};

using Register_t = void(__fastcall *)(int bank, uint32_t fieldOffset, uint32_t size,
                                      const void *callback, void *userArg, int kind);
using Unregister_t = void(__fastcall *)(int bank, uint32_t fieldOffset, const void *callback,
                                        void *userArg);

// Fires each of `slots` with (token, powerType) once per token of the unit,
// event by event as 4.3.4's handler signals them.
void Signal(uint64_t guid, int powerType, const int *slots, int count) {
    bool listened = false;
    for (int i = 0; i < count; ++i)
        listened = listened || Event::Custom::HasListeners(slots[i]);
    if (!listened)
        return;
    char tokens[16][32];
    const int n = Unit::Identity::TokensForGUID(guid, tokens, 16);
    const char *type = Unit::Power::PowerTypeToken(powerType);
    for (int i = 0; i < count; ++i)
        for (int t = 0; t < n; ++t)
            Event::Custom::Fire(slots[i], "%s%s", tokens[t], type);
}

uint64_t Guid(uint32_t lo, uint32_t hi) { return (static_cast<uint64_t>(hi) << 32) | lo; }

int __fastcall OnPowerChanged(uint32_t fieldOffset, uint32_t, uint32_t guidLo, uint32_t guidHi,
                              const void *, void *) {
    SignalPower(Guid(guidLo, guidHi),
                static_cast<int>(fieldOffset - Offsets::OFF_UNIT_FIELD_POWER1) / 4);
    return 1;
}

int __fastcall OnMaxPowerChanged(uint32_t fieldOffset, uint32_t, uint32_t guidLo,
                                 uint32_t guidHi, const void *, void *) {
    SignalMaxPower(Guid(guidLo, guidHi),
                   static_cast<int>(fieldOffset - Offsets::OFF_UNIT_FIELD_MAXPOWER1) / 4);
    return 1;
}

void Watch(uint32_t fieldOffset, const void *callback) {
    const auto unreg = reinterpret_cast<Unregister_t>(Offsets::FUN_TYPE_OBSERVER_UNREGISTER);
    const auto reg = reinterpret_cast<Register_t>(Offsets::FUN_TYPE_OBSERVER_REGISTER);
    unreg(Offsets::DESC_OBSERVER_BANK_UNIT, fieldOffset, callback, nullptr);
    reg(Offsets::DESC_OBSERVER_BANK_UNIT, fieldOffset, 4, callback, nullptr, 0);
}

void RegisterWatchers() {
    for (int type = 0; type < Offsets::UNIT_POWER_TYPE_COUNT; ++type) {
        Watch(Offsets::OFF_UNIT_FIELD_POWER1 + type * 4,
              reinterpret_cast<const void *>(&OnPowerChanged));
        Watch(Offsets::OFF_UNIT_FIELD_MAXPOWER1 + type * 4,
              reinterpret_cast<const void *>(&OnMaxPowerChanged));
    }
}

const Game::ModuleAutoRegister _autoreg{&RegisterWatchers};

} // namespace

void SignalPower(uint64_t guid, int powerType) {
    const int slots[] = {kUpdate.Slot(), kFrequent.Slot()};
    Signal(guid, powerType, slots, 2);
}

void SignalMaxPower(uint64_t guid, int powerType) {
    const int slots[] = {kMax.Slot()};
    Signal(guid, powerType, slots, 1);
}

} // namespace Unit::PowerEvents
