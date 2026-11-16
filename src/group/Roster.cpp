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

#include "group/Roster.h"

#include "Game.h"
#include "Offsets.h"

namespace Group::Roster {

uint64_t PartyGuid(int slot0) {
    if (slot0 < 0 || slot0 >= Offsets::PARTY_MAX_SLOTS)
        return 0;
    return Game::Read<uint64_t>(Offsets::VAR_PARTY_GUIDS + slot0 * sizeof(uint64_t));
}

int PartyCount() {
    int n = 0;
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i)
        n += PartyGuid(i) != 0 ? 1 : 0;
    return n;
}

bool IsPartyMember(uint64_t guid) {
    if (guid == 0)
        return false;
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i)
        if (PartyGuid(i) == guid)
            return true;
    return false;
}

int RaidCount() {
    const int count = Game::Read<int>(Offsets::VAR_RAID_MEMBER_COUNT);
    return count < 0 ? 0 : count > Offsets::RAID_MAX_SLOTS ? Offsets::RAID_MAX_SLOTS : count;
}

const uint8_t *RaidMember(int slot0) {
    if (slot0 < 0 || slot0 >= RaidCount())
        return nullptr;
    return Game::Read<const uint8_t *>(Offsets::VAR_RAID_MEMBER_PTRS + slot0 * sizeof(void *));
}

uint64_t RaidGuid(int slot0) {
    const uint8_t *member = RaidMember(slot0);
    return member != nullptr ? Game::Read<uint64_t>(member, 0) : 0;
}

bool RaidMemberOnline(const uint8_t *member) {
    return member != nullptr &&
           (Game::Read<uint32_t>(member, Offsets::OFF_RAID_SLOT_FLAGS) & Offsets::RAID_SLOT_FLAG_ONLINE) != 0;
}

bool IsRaidMember(uint64_t guid) {
    if (guid == 0)
        return false;
    for (int i = 0, n = RaidCount(); i < n; ++i)
        if (RaidGuid(i) == guid)
            return true;
    return false;
}

} // namespace Group::Roster
