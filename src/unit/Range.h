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

// What DistanceSquared found.
enum class Distance {
    NoFromUnit, // no FromUnit, or it has no position
    NoPosition, // the unit is not in view and has no member record
    OtherMap,   // the unit's member record is on another map than yours
    Measured,
};

// 3.80.2's distance core (0x141BB0FD0) without its gate: the squared x/y
// distance from FromUnit to `guid`, rounded half away from zero to a whole
// number, in `*squared` (0 unless Measured). A unit in view is measured at its
// position, a member out of view at its member record's. 3.80.2 does not
// compare maps, so it measures a record on another continent across two
// coordinate spaces. Here that is OtherMap, the maps named as the client names
// them: AreaTable's map column for both areas (VAR_PLAYER_AREA_ID is what the
// record builder 0x005F0880 stores as the area).
Distance DistanceSquared(uint64_t guid, long long *squared);

// UnitInRange's check for `guid` (3.80.2's 0x141BB0D80): returns
// checkedRange, true for any group unit (Group::Roster::IsGroupUnit) while a
// FromUnit exists, and sets `*inRange` when the unit is in view and within
// reach of a 40-yard heal. Any other unit is (false, false).
bool InRange(uint64_t guid, bool *inRange);

} // namespace Unit::Range
