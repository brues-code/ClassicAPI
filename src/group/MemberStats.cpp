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

#include "group/MemberStats.h"

#include "Game.h"
#include "Offsets.h"

namespace Group::MemberStats {

namespace {

// Both roster lookups are `__fastcall(u64 *guid) -> block*` (NULL on miss).
using Lookup_t = const uint8_t *(__fastcall *)(uint64_t *guid);

uint16_t U16(const uint8_t *p, int off) {
    return Game::Read<uint16_t>(p, off);
}

} // namespace

Stats Lookup(uint64_t guid) {
    Stats s;
    if (guid == 0)
        return s;

    // Party stats block — the full record.
    auto statsFn = reinterpret_cast<Lookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_GROUP_MEMBER_STATS_LOOKUP));
    if (const uint8_t *e = statsFn(&guid)) {
        s.valid = true;
        s.full = true;
        s.powerType = e[Offsets::OFF_GROUP_MEMBER_STATS_POWER_TYPE];
        s.health = U16(e, Offsets::OFF_GROUP_MEMBER_STATS_HEALTH);
        s.maxHealth = U16(e, Offsets::OFF_GROUP_MEMBER_STATS_MAX_HEALTH);
        s.power = U16(e, Offsets::OFF_GROUP_MEMBER_STATS_POWER);
        s.maxPower = U16(e, Offsets::OFF_GROUP_MEMBER_STATS_MAX_POWER);
        s.level = U16(e, Offsets::OFF_GROUP_MEMBER_STATS_LEVEL);
        s.areaId = U16(e, Offsets::OFF_GROUP_MEMBER_AREA_ID);
        s.online = (e[Offsets::OFF_GROUP_MEMBER_STATUS_FLAGS] &
                    Offsets::GROUP_MEMBER_STATUS_ONLINE) != 0;
        // The party stats block carries no dead / subgroup / rank fields; those
        // are raid-only (dead comes from UnitIsDead on the live unit otherwise).
        return s;
    }

    // Raid slot — same struct GetRaidRosterInfo reads.
    auto slotFn = reinterpret_cast<Lookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_GROUP_MEMBER_SLOT_LOOKUP));
    if (const uint8_t *e = slotFn(&guid)) {
        s.valid = true;
        s.full = false;
        s.powerType = e[Offsets::OFF_RAID_SLOT_POWER_TYPE];
        s.health = U16(e, Offsets::OFF_RAID_SLOT_HEALTH);
        s.maxHealth = U16(e, Offsets::OFF_RAID_SLOT_MAX_HEALTH);
        s.power = U16(e, Offsets::OFF_RAID_SLOT_POWER);
        s.maxPower = U16(e, Offsets::OFF_RAID_SLOT_MAX_POWER);
        s.level = U16(e, Offsets::OFF_RAID_SLOT_LEVEL);
        s.areaId = U16(e, Offsets::OFF_RAID_SLOT_AREA_ID);
        const uint32_t flags =
            Game::Read<uint32_t>(e, Offsets::OFF_RAID_SLOT_FLAGS);
        s.online = (flags & Offsets::RAID_SLOT_FLAG_ONLINE) != 0;
        s.dead = (flags & Offsets::RAID_SLOT_FLAG_DEAD) != 0;
        s.subgroup = static_cast<int>(Game::Read<uint32_t>(
                         e, Offsets::OFF_RAID_SLOT_SUBGROUP)) + 1;
        s.rank = static_cast<int>(
            Game::Read<uint32_t>(e, Offsets::OFF_RAID_SLOT_RANK));
        return s;
    }

    return s;
}

bool Position(uint64_t guid, float *x, float *y, uint32_t *areaId) {
    if (guid == 0)
        return false;
    auto statsFn = reinterpret_cast<Lookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_GROUP_MEMBER_STATS_LOOKUP));
    const uint8_t *e = statsFn(&guid);
    if (e == nullptr)
        return false;
    *x = Game::Read<int16_t>(e, Offsets::OFF_GROUP_MEMBER_POS_X);
    *y = Game::Read<int16_t>(e, Offsets::OFF_GROUP_MEMBER_POS_Y);
    *areaId = U16(e, Offsets::OFF_GROUP_MEMBER_AREA_ID);
    return true;
}

const uint16_t *AuraArray(uint64_t guid) {
    if (guid == 0)
        return nullptr;

    // Party stats block first — auras at +0x1a, but only for an online member
    // (the engine's Script_UnitBuff bails on the party path when the online bit
    // is clear).
    auto statsFn = reinterpret_cast<Lookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_GROUP_MEMBER_STATS_LOOKUP));
    if (const uint8_t *e = statsFn(&guid)) {
        if ((e[Offsets::OFF_GROUP_MEMBER_STATUS_FLAGS] &
             Offsets::GROUP_MEMBER_STATUS_ONLINE) == 0)
            return nullptr;
        return Game::Ptr<const uint16_t>(
            e, Offsets::OFF_GROUP_MEMBER_STATS_AURAS);
    }

    // Raid slot — auras at +0x64, no online gate (mirrors the engine's raid
    // branch, which reads the array unconditionally).
    auto slotFn = reinterpret_cast<Lookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_GROUP_MEMBER_SLOT_LOOKUP));
    if (const uint8_t *e = slotFn(&guid)) {
        return Game::Ptr<const uint16_t>(
            e, Offsets::OFF_RAID_SLOT_AURAS);
    }

    return nullptr;
}

} // namespace Group::MemberStats
