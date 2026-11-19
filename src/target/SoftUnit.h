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

namespace Target::SoftUnit {

enum class Side { Enemy, Friend };

// The `softenemy` / `softfriend` GUID, or 0: the frame tick's last result.
// Read by the token resolver in `unit/TokenExtensions.cpp`.
uint64_t Get(Side side);

// The same, for the unit-event hot path (`unit/TokenEvents.cpp`).
uint64_t Current(Side side);

// SoftTargetEnemy / SoftTargetFriend turns the side on.
bool Enabled(Side side);

// SoftTargetEnemyArc / SoftTargetFriendArc.
int Arc(Side side);

// The `anyenemy` / `anyfriend` GUID: with no soft unit, the target; the soft
// unit while the target is empty, loose or the soft unit itself; else the
// target when it is a candidate of the side, or the soft unit.
uint64_t GetAny(Side side);

} // namespace Target::SoftUnit
