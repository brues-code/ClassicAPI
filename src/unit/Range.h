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

namespace Unit::Range {

// The unit range checks measure from: the active mover (the unit the client
// moves: the player, or a unit it controls), else the player object. Null
// with neither.
void *FromUnit();

// UnitInRange's check for `guid` (3.80.2's 0x141BB0D80): returns
// checkedRange, true for any group unit (Group::Roster::IsGroupUnit) while a
// FromUnit exists, and sets `*inRange` when the unit is in view and within
// reach of a 40-yard heal. Any other unit is (false, false).
bool InRange(uint64_t guid, bool *inRange);

} // namespace Unit::Range
