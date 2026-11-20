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

// `UNIT_TARGETABLE_CHANGED` and `IsUnitUntargetable`, as the 3.80.2 Classic
// client has them.
//
// 3.80.2's unit-flags handler (0x14149A080) acts when UNIT_FLAG_NOT_SELECTABLE
// flips. When the bit is gained, it first forgets the unit everywhere
// (0x1405DEBB0): the soft targets are re-picked if the unit is the target or
// a soft target, the target is cleared, the last-target slot that holds it is
// zeroed, and the focus is dropped. Then, in both directions, it queues the
// event (0x140EBCBC0): one pending entry per unit, fired at the frame's event
// drain (0x140E9E460) once per token the unit has then (0x141B90E10), payload
// the token only.
//
// 1.12's CGUnit_C::OnUnitFlagsChanged has no branch for this bit, so this
// subscribes to Unit::FlagsChanged, which runs after it, and drains on the
// frame tick. 3.80.2 also runs the handler when a unit is created already
// unselectable (old flags 0); 1.12 runs no field watcher for a new object, but
// such a unit has no target, focus or soft-target reference to drop and
// hardly any token at the drain. 3.80.2's second source, UNIT_FIELD_FLAGS_2
// bit 0x10000000 on a hostile unit (0x14149A640), has no 1.12 field.
//
// IsUnitUntargetable (0x141B85CB0) is true for a player with the "untargetable
// by anyone" GM state, a UNIT_FIELD_FLAGS_3 bit. 1.12 has no third flags field;
// the GM state there is vmangos's `.cheat untargetable`, which sets
// UNIT_FLAG_NOT_SELECTABLE on the player (and the server clears that bit in
// what it sends to a GM). So here it reads that bit on a player.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "target/Priority.h"
#include "target/Reason.h"
#include "tick/FrameTick.h"
#include "unit/FlagsChanged.h"
#include "unit/Focus.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Unit::Targetable {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
};
const Game::Doc::Event kDoc{
    "Unit", "Fires when the unit becomes selectable or stops being selectable.", kPayload};
const Event::Custom::AutoReserve kChanged{"UNIT_TARGETABLE_CHANGED", &kDoc};

std::vector<uint64_t> g_pending;

// 0x1405DEBB0 for a unit that became unselectable.
void Forget(uint64_t guid) {
    if (Target::Reason::Selection() == guid || Target::Reason::IsSoftTarget(guid))
        Target::Priority::Invalidate();
    if (Target::Reason::Selection() == guid)
        Target::Reason::Set(0, Target::Reason::Chosen);
    auto &last = Game::Ref<uint64_t>(Offsets::VAR_LAST_SELECTION_GUID_LO);
    if (last == guid)
        last = 0;
    if (Unit::Focus::Get() == guid)
        Unit::Focus::Set(0);
}

void OnFlags(void *unit, uint32_t oldFlags, uint32_t flags) {
    if (((flags ^ oldFlags) & Offsets::UNIT_FLAG_NOT_SELECTABLE) == 0)
        return;
    const uint64_t guid = ::Object::Guid(unit);
    if ((flags & Offsets::UNIT_FLAG_NOT_SELECTABLE) != 0)
        Forget(guid);
    if (std::find(g_pending.begin(), g_pending.end(), guid) == g_pending.end())
        g_pending.push_back(guid);
}

const Unit::FlagsChanged::AutoSubscribe _sub{&OnFlags};

void OnFrameTick() {
    if (g_pending.empty())
        return;
    std::vector<uint64_t> fire;
    fire.swap(g_pending); // a handler can queue the next drain's entries
    const int slot = kChanged.Slot();
    if (!Event::Custom::HasListeners(slot))
        return;
    for (const uint64_t guid : fire)
        Unit::Identity::ForEachToken(guid,
                                     [&](const char *token) { Event::Custom::Fire(slot, "%s", token); });
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

void PrepareForReload() {
    g_pending.clear();
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

int __fastcall Script_IsUnitUntargetable(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: IsUnitUntargetable(\"unit\")");
        return 0;
    }
    bool untargetable = false;
    if (void *unit = Game::ResolveUnitToken(Game::Lua::ToString(L, 1))) {
        const uint64_t guid = ::Object::Guid(unit);
        const auto *fields = ::Object::Fields(unit);
        untargetable = fields != nullptr &&
                       ::Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid) != nullptr &&
                       (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
                        Offsets::UNIT_FLAG_NOT_SELECTABLE) != 0;
    }
    Game::Lua::PushBool(L, untargetable);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("untargetable", "bool",
                   "True for a player that no one can target; false for any other unit."),
};
const Game::Doc::Function kIsUnitUntargetable{
    "Returns whether a player is untargetable by everyone.", kArgs, kRets, "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsUnitUntargetable", &Script_IsUnitUntargetable,
                                      &kIsUnitUntargetable);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Targetable
