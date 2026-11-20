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

#pragma once

#include <cstdint>

// The local player's group, read from the engine's own tables.
//
// The party is VAR_PARTY_GUIDS, PARTY_MAX_SLOTS GUIDs, 0 for an empty slot
// (partyN is slot N-1).
//
// The raid roster is VAR_RAID_MEMBER_PTRS, RAID_MAX_SLOTS pointers to records
// that InitializeGame allocates once (0x004BA114), with the member count at
// VAR_RAID_MEMBER_COUNT. RebuildRaidRoster (0x004BA620) fills slots
// 0..count-1 in order and writes no slot past the new count, so a slot past
// it still holds whoever had it before. The engine's IsRaidMember
// (0x004BAF00) walks only `< count`, and so does everything here. raidN is
// slot N-1. A record holds the GUID at +0x00 and the flags at
// OFF_RAID_SLOT_FLAGS.
namespace Group::Roster {

// partyN's GUID for slot `slot0` (0..3), 0 when the slot is empty.
uint64_t PartyGuid(int slot0);

// The number of filled party slots, as Script_GetNumPartyMembers counts them.
int PartyCount();

bool IsPartyMember(uint64_t guid);

// The raid roster's size, 0 outside a raid.
int RaidCount();

// raid slot `slot0`'s record while it is inside the roster, else nullptr.
const uint8_t *RaidMember(int slot0);

// raid slot `slot0`'s GUID while it is inside the roster, else 0.
uint64_t RaidGuid(int slot0);

// Whether a raid slot's member is online (RAID_SLOT_FLAG_ONLINE).
bool RaidMemberOnline(const uint8_t *member);

// IsRaidMember (0x004BAF00): `guid` is on the roster.
bool IsRaidMember(uint64_t guid);

// While you are in a party or raid: `guid` is you, a member, or the pet of
// one of these (FUN_UNIT_IS_GROUP_MEMBER_OR_PET). False when solo. This is
// the 3.80.2 client's group gate (its member list holds you only while you
// are in a group): UnitInRange answers only for these units.
bool IsGroupUnit(uint64_t guid);

} // namespace Group::Roster
