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

// The `trade:` profession link, 3.3.5's format and bit space. See Link.cpp.
namespace TradeSkill::Link {

// Upper bound on a skill line's SkillLineAbility rows (Blacksmithing, the
// largest, has 343).
constexpr int MAX_ABILITIES = 1024;

// Fills `out` with the SkillLineAbility records a link's bits cover for
// `skillLine`, one bit each, in DBC order; returns the count (at most
// MAX_ABILITIES). 3.3.5's SpellTableGetSkillLineLinkInfo.
int Abilities(uint32_t skillLine, const uint8_t **out);

// The number of bit characters a link carries for `count` rows.
int BitChars(int count);

// Whether row `index` is set in a link's bit characters.
bool IsSet(const char *bits, int index);

// 3.3.5's CGTradeSkillInfo::SetLink: shows the profession a received link
// carries. `data` is the link payload after `trade:`. A linker the name
// cache does not hold yet is queried, and the window opens when the answer
// arrives.
void Open(const char *data);

} // namespace TradeSkill::Link
