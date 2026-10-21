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

// The `softenemy` / `softfriend` GUID, or 0. Read by the token resolver in
// `unit/TokenExtensions.cpp`.
uint64_t Get(Side side);

// SoftTargetEnemy / SoftTargetFriend turns the side on.
bool Enabled(Side side);

// The `anyenemy` / `anyfriend` GUID: the current target when it qualifies as
// that side's soft target, else Get(side), else the current target, else the
// other side's soft unit.
uint64_t GetAny(Side side);

} // namespace Target::SoftUnit
