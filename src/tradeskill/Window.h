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

// Whose profession the TradeSkill and Craft windows show. See Window.cpp.
namespace TradeSkill::Window {

// The profession spell the TradeSkill window shows (3.3.5's
// CGTradeSkillInfo::m_spellID), or 0 while the window is closed.
int TradeSkillSpell();

// The Craft window's twin of TradeSkillSpell.
int CraftSpell();

// 3.3.5's CGTradeSkillInfo::SetTradeSkill for a received link: shows
// profession `spellID` of player `guid`, at `rank` / `maxRank`, with the
// rows set in the link's `bits`, in the window the spell opens when cast.
// The name cache must hold `guid`.
void ShowLinked(int spellID, uint64_t guid, const char *bits, int rank, int maxRank);

} // namespace TradeSkill::Window
