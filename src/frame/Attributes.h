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

namespace Frame::Attributes {

// Drop the per-frame attribute-handler and unit-token maps before a /reload (or
// logout) resets the Lua state and recycles frame object pointers. Without it, a
// stale OnAttributeChanged ref fires from a new frame that reused an old
// address (surfaces as "SetAttribute occasionally errors after /reload"). Wired
// from `FrameScript_Initialize_h` in DllMain.cpp alongside the other cleaners.
void PrepareForReload();

// Drops every attribute of the frame at stack index `frameIdx`, and its
// mouseover binding, without running OnAttributeChanged: retail's
// SetToDefaults.
void ClearAll(void *L, int frameIdx);

} // namespace Frame::Attributes
