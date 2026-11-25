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

// Gamepad buttons and sticks to frames and key bindings, and the buttons that
// emulate Shift / Ctrl / Alt, as the 3.80.2 client sends them (its per-frame
// edge loop 0x14125B710).
namespace GamePad::Buttons {

// One frame's button and stick events for the active device, after the poll
// has mapped it.
void Dispatch();

// Releases everything the buttons hold (captured buttons, running bindings,
// emulated modifiers), as when gamepad support stops.
void Reset();

// True when the stick's event this frame reached no frame that took it (the
// binding fallback's 0x1405A1190): the stick is free to drive the character
// or the camera.
bool StickUnclaimed(int stick);

} // namespace GamePad::Buttons
