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

namespace Group::MemberStats {

// Cached stats for a party/raid member, read from the group roster
// (fed by SMSG_PARTY_MEMBER_STATS). This is the ONLY source for a member with
// no live CGUnit — a groupmate on a different map or otherwise out of range —
// and is exactly what the engine's UnitHealth / UnitMana fall back to. Any
// module that reads unit fields off the descriptor should consult this when
// the descriptor is unavailable so it doesn't report 0 for out-of-range
// groupmates.
//
// `full` distinguishes the two backing structures: the party stats block and
// the raid slot. Both carry health / power / level / area / online; `dead`,
// `subgroup`, and `rank` come only from the raid slot (0 / false on the party
// path). Power values are RAW (divide by the power divisor for display);
// health is the roster u16, matching what UnitHealth returns out of range.
struct Stats {
    bool valid = false;
    bool full = false;       // true = party stats block; false = raid slot
    int powerType = 0;
    uint32_t health = 0;
    uint32_t maxHealth = 0;
    uint32_t power = 0;      // raw (pre-divisor)
    uint32_t maxPower = 0;   // raw (pre-divisor)
    int level = 0;
    int areaId = 0;          // AreaTable.dbc ID
    bool online = false;
    bool dead = false;       // raid slot only
    int subgroup = 0;        // raid slot only, 1-based (0 = party path)
    int rank = 0;            // raid slot only
};

// Look up a member by GUID. Tries the party stats block, then the raid slot,
// mirroring the engine's UnitHealth/UnitMana out-of-range fallback. Returns an
// invalid `Stats` (`valid == false`) when the GUID isn't a rostered member.
// Pure — no Lua, no throw; safe from any context.
Stats Lookup(uint64_t guid);

// Pointer to an out-of-range groupmate's 48-slot aura spell-ID array (buffs
// 0..31, debuffs 32..47), or nullptr if `guid` isn't a rostered member — or is
// an *offline* party member (the engine surfaces no auras for those). Mirrors
// the engine's own out-of-range fallback in Script_UnitBuff/Script_UnitDebuff
// exactly: the party stats block at +OFF_GROUP_MEMBER_STATS_AURAS gated on the
// online flag, else the raid slot at +OFF_RAID_SLOT_AURAS. Spell IDs only —
// SMSG_PARTY_MEMBER_STATS carries no caster/duration/stacks, and truncates IDs
// to u16 (custom IDs > 65535 are wrong; same as the built-in UnitBuff). The
// pointer is into live engine memory — read it immediately, don't cache it.
// Pure — no Lua, no throw; safe from any context.
const uint16_t *AuraArray(uint64_t guid);

// An out-of-view member's last reported position (x, y; truncated to whole
// yards) and its area (AreaTable ID), from the member record the engine keeps
// for party and raid members (FUN_GROUP_MEMBER_STATS_LOOKUP). False when the
// GUID has no member record (not a member, or a pet).
bool Position(uint64_t guid, float *x, float *y, uint32_t *areaId);

} // namespace Group::MemberStats
