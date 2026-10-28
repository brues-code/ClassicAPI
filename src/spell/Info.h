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

namespace Spell::Info {

// The classifier behind `IsHarmfulSpell` / `IsHelpfulSpell`: whether any
// effect of the spell aims at an enemy / at a friend (see the target sets in
// Info.cpp). False for an unknown spell. The two are not exclusive.
bool IsHarmful(int spellID);
bool IsHelpful(int spellID);

} // namespace Spell::Info
