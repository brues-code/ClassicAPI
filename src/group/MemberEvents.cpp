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

// `UNIT_CONNECTION` and the generic power events for a group member's party
// record.
//
// A group member's stats packet (ReadPartyMemberStats, 0x005E5110) and the
// player's own record check (ComparePartyRecord, 0x005F0B80) end in
// SignalPartyRecordChanges (FUN_PARTY_RECORD_SIGNAL), which raises the unit
// events for the fields that differ between the old and the new record. This
// is the 1.12 copy of 4.3.4's CGPlayer_C::DispatchPartyMemberStatsChanges
// (Player_C.cpp), which in the same places also signals UNIT_CONNECTION
// `(token, isConnected)` when the online bit of the status flags flips, and
// UNIT_POWER / UNIT_POWER_FREQUENT / UNIT_MAXPOWER with the power token
// beside each per-type power event.
//
// Both calls point here instead (Common::RedirectCall): the function calls
// the raiser, then adds those events for the same change bits, after the
// engine's own events and with the new record already stored, so
// UnitIsConnected and UnitPower read the new values. A status change also
// raises the power event, and the raiser then drops the health and power
// bits, so the power events follow that order exactly.
//
// A member who logs off is reported only in the group list (SMSG 0x7D):
// tortoise-wow's Group::UpdatePlayerOnlineStatus sends SendUpdate alone for
// a logout. 4.3.4's CGPartyInfo::AddMember ends that path with the same
// diff and dispatch; 1.12's AddPartyMember stops after storing the record,
// so its call gets the missing tail. A raid member outside the player's
// party has no party record: RebuildRaidRoster sets its online bit in the
// raid slot, so the online bits before and after that call are compared,
// and a flip fires UNIT_CONNECTION, as retail's raid frames expect. Party
// members are left to AddPartyMember, which ran first.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "unit/Identity.h"
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

const Game::Doc::Field kConnectionPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("isConnected", "bool"),
};
const Game::Doc::Event kConnectionDoc{
    "Unit", "Fires when a group member goes offline or comes back online.", kConnectionPayload};
const Event::Custom::AutoReserve kConnection{"UNIT_CONNECTION", &kConnectionDoc};

using Signal_t = void(__fastcall *)(const uint64_t *guid, uint32_t changes, const uint8_t *oldRec,
                                    const uint8_t *newRec);
using Diff_t = uint32_t(__fastcall *)(const uint8_t *oldRec, const uint8_t *newRec);
using StatsLookup_t = const uint8_t *(__fastcall *)(const uint64_t *guid);
using AddMember_t = void(__fastcall *)(uint32_t online, const uint8_t *oldRecord, uint64_t guid);
using RebuildRaid_t = void(__fastcall *)(const uint64_t *guids, const uint32_t *flags,
                                         const uint8_t *online, uint32_t count);

void SignalConnection(uint64_t guid, bool connected) {
    const int slot = kConnection.Slot();
    if (!Event::Custom::HasListeners(slot))
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    char tokens[16][32];
    const int n = Unit::Identity::TokensForGUID(guid, tokens, 16);
    const int top = Game::Lua::GetTop(L);
    for (int i = 0; i < n; ++i) {
        Game::Lua::PushString(L, tokens[i]);
        Game::Lua::PushBool(L, connected);
        Event::Custom::FireValues(slot, L, top + 1, 2);
        Game::Lua::SetTop(L, top);
    }
}

void __fastcall SignalSite(const uint64_t *guid, uint32_t changes, const uint8_t *oldRec,
                           const uint8_t *newRec) {
    reinterpret_cast<Signal_t>(Offsets::FUN_PARTY_RECORD_SIGNAL)(guid, changes, oldRec, newRec);
    const uint64_t member = *guid;
    const int powerType = Game::Read<uint8_t>(newRec, Offsets::OFF_GROUP_MEMBER_STATS_POWER_TYPE);
    if (changes & CHANGE_STATUS) {
        Unit::PowerEvents::SignalPower(member, powerType);
        const uint8_t status = Game::Read<uint8_t>(newRec, Offsets::OFF_GROUP_MEMBER_STATUS_FLAGS);
        const uint8_t flips =
            status ^ Game::Read<uint8_t>(oldRec, Offsets::OFF_GROUP_MEMBER_STATUS_FLAGS);
        if (flips & Offsets::GROUP_MEMBER_STATUS_ONLINE)
            SignalConnection(member, (status & Offsets::GROUP_MEMBER_STATUS_ONLINE) != 0);
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

void __fastcall AddMemberSite(uint32_t online, const uint8_t *oldRecord, uint64_t guid) {
    reinterpret_cast<AddMember_t>(Offsets::FUN_ADD_PARTY_MEMBER)(online, oldRecord, guid);
    if (oldRecord == nullptr)
        return;
    const uint8_t *record =
        reinterpret_cast<StatsLookup_t>(Offsets::FUN_GROUP_MEMBER_STATS_LOOKUP)(&guid);
    if (record == nullptr)
        return;
    const uint32_t changes =
        reinterpret_cast<Diff_t>(Offsets::FUN_PARTY_RECORD_DIFF)(oldRecord, record);
    if (changes != 0)
        SignalSite(&guid, changes, oldRecord, record);
}

struct Presence {
    uint64_t guid;
    bool online;
};

// The raid slots' GUIDs and online bits.
int RaidPresence(Presence (&out)[Offsets::RAID_MAX_SLOTS]) {
    int count = Game::Read<int>(Offsets::VAR_RAID_MEMBER_COUNT);
    if (count > Offsets::RAID_MAX_SLOTS)
        count = Offsets::RAID_MAX_SLOTS;
    const auto *slots = reinterpret_cast<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_RAID_MEMBER_PTRS));
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (slots[i] == nullptr)
            continue;
        const uint8_t flags = Game::Read<uint8_t>(slots[i], Offsets::OFF_RAID_SLOT_FLAGS);
        out[n++] = {Game::Read<uint64_t>(slots[i], 0),
                    (flags & Offsets::RAID_SLOT_FLAG_ONLINE) != 0};
    }
    return n;
}

bool IsPartyMember(uint64_t guid) {
    const auto *party =
        reinterpret_cast<const uint64_t *>(static_cast<uintptr_t>(Offsets::VAR_PARTY_GUIDS));
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i)
        if (party[i] == guid)
            return true;
    return false;
}

void __fastcall RaidRosterSite(const uint64_t *guids, const uint32_t *flags,
                               const uint8_t *online, uint32_t count) {
    Presence before[Offsets::RAID_MAX_SLOTS];
    const int was = RaidPresence(before);
    reinterpret_cast<RebuildRaid_t>(Offsets::FUN_REBUILD_RAID_ROSTER)(guids, flags, online, count);
    if (!Event::Custom::HasListeners(kConnection.Slot()))
        return;
    Presence after[Offsets::RAID_MAX_SLOTS];
    const int now = RaidPresence(after);
    for (int i = 0; i < now; ++i) {
        if (IsPartyMember(after[i].guid))
            continue;
        for (int j = 0; j < was; ++j) {
            if (before[j].guid == after[i].guid) {
                if (before[j].online != after[i].online)
                    SignalConnection(after[i].guid, after[i].online);
                break;
            }
        }
    }
}

void Register() {
    const auto *site = reinterpret_cast<const void *>(&SignalSite);
    Common::RedirectCall(Offsets::CALL_READ_PARTY_MEMBER_STATS_SIGNAL,
                         Offsets::FUN_PARTY_RECORD_SIGNAL, site);
    Common::RedirectCall(Offsets::CALL_COMPARE_PARTY_RECORD_SIGNAL,
                         Offsets::FUN_PARTY_RECORD_SIGNAL, site);
    Common::RedirectCall(Offsets::CALL_GROUP_LIST_ADD_PARTY_MEMBER, Offsets::FUN_ADD_PARTY_MEMBER,
                         reinterpret_cast<const void *>(&AddMemberSite));
    Common::RedirectCall(Offsets::CALL_GROUP_LIST_REBUILD_RAID_ROSTER,
                         Offsets::FUN_REBUILD_RAID_ROSTER,
                         reinterpret_cast<const void *>(&RaidRosterSite));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Group::MemberEvents
