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

// World-geometry line of sight between two resolved objects, shared by
// `UnitInLineOfSight` and any module that needs the engine's own trace
// (e.g. `UnitIsBehind`'s stationary-NPC facing compensation).

namespace Unit::LineOfSight {

// One segment, each end raised by its own unit's collision height — the
// server's check (see the .cpp header). Returns 1 = in sight, 0 = blocked,
// -1 = unknown (a position could not be read).
int InSight(void *a, void *b);

} // namespace Unit::LineOfSight
