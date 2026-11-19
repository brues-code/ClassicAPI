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

// The talent preview plan (Preview.cpp), for the preview talent tooltip.
namespace Talent::Preview {

// The local player, with the plan of another character dropped. Null out of
// the world.
const uint8_t *Player();

// The row of the 0-based talent in the 0-based tab, or null.
const uint8_t *TalentRow(int tab, int index);

// The row in the player's tabs that has `spellID` in a rank slot, with the
// 0-based slot. Null when no talent of the player has the spell.
const uint8_t *RowOfRankSpell(uint32_t spellID, int *slot);

// The rank the plan shows: the planned rank, else the learned one.
int PreviewRank(const uint8_t *player, const uint8_t *row);

// The last rank slot with a spell, plus 1.
int MaxRank(const uint8_t *row);

// The points planned in a tab, by its TabInfo.
int TabPlanned(const uint8_t *player, const uint8_t *tabInfo);

// Whether AddPreviewTalentPoints would change the plan by `points`.
bool CanChange(const uint8_t *player, const uint8_t *row, int points);

// The isPet and groupIndex arguments at those Lua stack slots name the
// player's own talent group 1.
bool IsPlayerGroup(void *L, int isPetIndex, int groupIndex);

} // namespace Talent::Preview
