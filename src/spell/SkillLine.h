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

// The engine's skill-line lookups.
namespace Spell::SkillLine {

// A player's entry in one skill line, as the CGPlayer `+0xE68` skill table
// holds it: `rank` and `maxRank` are the base values, `bonus` the permanent
// bonus GetTradeSkillLine and GetSkillLineInfo add to a nonzero base.
struct Rank {
    int rank;
    int maxRank;
    int bonus;
};

// `player`'s entry for SkillLine.dbc row `skillLine`: FUN_SKILL_LINE_TO_SLOT
// maps the line to a slot, and the slot indexes the skill table. False when
// the player has no slot for the line or the table is not resident.
bool PlayerRank(const uint8_t *player, uint32_t skillLine, Rank *out);

// The SkillLineAbility.dbc row the engine picks for `spellID` and a
// race/class pair (FUN_SKILL_LINE_ABILITY_LOOKUP), or nullptr.
const uint8_t *AbilityFor(uint32_t race, uint32_t classId, uint32_t spellID);

} // namespace Spell::SkillLine
