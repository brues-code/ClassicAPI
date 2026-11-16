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

// A unit's fall, as the client's mover computes it: the arc every jump and
// every fall off a ledge follows. Shared by the ModelScene mover port and the
// replay reader, which draws a recorded jump with it.
namespace Unit::Fall {

// CMovement::GetFallDistance (0x007C5E70): how far below the fall's start a
// unit is `seconds` into a fall that began at `fallSpeed` (negative is up:
// a jump launches at about -7.96), the speed capped at the terminal speed
// (the slow fall's cap with `slowFall`, movement flag 0x20000000). Negative
// while a jump is still above its start.
double Distance(float seconds, float fallSpeed, bool slowFall);

} // namespace Unit::Fall
