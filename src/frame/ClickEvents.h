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

namespace Frame::ClickEvents {

// Drop every per-button PreClick/PostClick handler cell before a `/reload` (or
// logout) tears down the button objects and resets the Lua state. The handler
// refs die with the Lua reset, and a recycled button address must not fire a
// stale ref. Wired from `FrameScript_Initialize_h` in DllMain.cpp, alongside
// the other per-reload cleaners.
void PrepareForReload();

// The button name of the innermost click / mouse dispatch running now (one of
// the engine's static name literals), or null outside one — what
// GetMouseButtonClicked returns; 4.3.4's CSimpleTop::m_mouseButtonName.
const char *CurrentButton();

} // namespace Frame::ClickEvents
