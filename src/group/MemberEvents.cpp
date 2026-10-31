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

// The generic power events for a group member's party record.
//
// A group member's stats packet (ReadPartyMemberStats, 0x005E5110) and the
// player's own record check (ComparePartyRecord, 0x005F0B80) end in
// SignalPartyRecordChanges (FUN_PARTY_RECORD_SIGNAL), which raises the unit
// events for the fields that differ between the old and the new record. This
// is the 1.12 copy of 4.3.4's CGPlayer_C::DispatchPartyMemberStatsChanges
// (Player_C.cpp), which in the same places also signals UNIT_POWER /
// UNIT_POWER_FREQUENT / UNIT_MAXPOWER with the power token beside each
// per-type power event.
//
// Both calls point here instead (Common::RedirectCall): the function calls
// the raiser, then adds those events for the same change bits, after the
// engine's own events and with the new record already stored, so UnitPower
// reads the new values. A status change also raises the power event, and the
// raiser then drops the health and power bits, so the power events follow
// that order exactly.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "unit/PowerEvents.h"

#include <cstdint>

namespace Group::MemberEvents {

namespace {

// DiffPartyRecords (0x005E5510) change bits the raiser reads.
constexpr uint32_t CHANGE_STATUS = 0x1;
constexpr uint32_t CHANGE_HEALTH = 0x2;
constexpr uint32_t CHANGE_POWER = 0x10;
constexpr uint32_t CHANGE_MAX_POWER = 0x20;
constexpr uint32_t CHANGE_PET_POWER = 0x20000;
constexpr uint32_t CHANGE_PET_MAX_POWER = 0x40000;

using Signal_t = void(__fastcall *)(const uint64_t *guid, uint32_t changes, const uint8_t *oldRec,
                                    const uint8_t *newRec);

void __fastcall SignalSite(const uint64_t *guid, uint32_t changes, const uint8_t *oldRec,
                           const uint8_t *newRec) {
    reinterpret_cast<Signal_t>(Offsets::FUN_PARTY_RECORD_SIGNAL)(guid, changes, oldRec, newRec);
    const uint64_t member = *guid;
    const int powerType = Game::Read<uint8_t>(newRec, Offsets::OFF_GROUP_MEMBER_STATS_POWER_TYPE);
    if (changes & CHANGE_STATUS) {
        Unit::PowerEvents::SignalPower(member, powerType);
        changes &= ~(CHANGE_HEALTH | CHANGE_POWER);
    }
    if (changes & CHANGE_POWER)
        Unit::PowerEvents::SignalPower(member, powerType);
    if (changes & CHANGE_MAX_POWER)
        Unit::PowerEvents::SignalMaxPower(member, powerType);
    const uint64_t pet = Game::Read<uint64_t>(newRec, Offsets::OFF_GROUP_MEMBER_PET_GUID);
    if (pet == 0)
        return;
    const int petPowerType = Game::Read<uint8_t>(newRec, Offsets::OFF_GROUP_MEMBER_PET_POWER_TYPE);
    if (changes & CHANGE_PET_POWER)
        Unit::PowerEvents::SignalPower(pet, petPowerType);
    if (changes & CHANGE_PET_MAX_POWER)
        Unit::PowerEvents::SignalMaxPower(pet, petPowerType);
}

void Register() {
    const auto *site = reinterpret_cast<const void *>(&SignalSite);
    Common::RedirectCall(Offsets::CALL_READ_PARTY_MEMBER_STATS_SIGNAL,
                         Offsets::FUN_PARTY_RECORD_SIGNAL, site);
    Common::RedirectCall(Offsets::CALL_COMPARE_PARTY_RECORD_SIGNAL,
                         Offsets::FUN_PARTY_RECORD_SIGNAL, site);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Group::MemberEvents
