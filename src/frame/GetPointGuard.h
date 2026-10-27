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

namespace Frame::GetPointGuard {

// Drops every anchor on `region` whose relativeTo target is no longer
// readable, through the engine's own FUN_REGION_CLEAR_POINT_BY_RELATIVE —
// what the GetPoint override runs before the stock reader (see the .cpp).
// Anything that reads a region's anchor nodes itself calls this first.
void CleanDanglingAnchors(void *region);

// Pushes GetPoint's returns for `region`'s anchor at `point` (FRAMEPOINT_*):
// point, then relativeTo, relativePoint for a point with a target, then x
// and y. Returns the count pushed, 0 for an empty slot. Run
// CleanDanglingAnchors first.
int PushPoint(void *L, void *region, int point);

} // namespace Frame::GetPointGuard
