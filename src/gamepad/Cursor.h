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

namespace GamePad::State {
struct Mapped;
}

// The gamepad cursor, as the 3.80.2 client drives it: cursor mode (the
// Cursor stick and the touchpad move the OS cursor, two buttons click), the
// cursor hidden under gamepad freelook, and spell targeting from a pad
// binding.
namespace GamePad::Cursor {

// Cursor mode on or off (0x141264970). `touch`: entered by the touchpad.
void Mode(bool enable, bool touch);
bool ModeOn();
bool TouchMode();

// Whether the click buttons carry the clicks (0x141264AD0): cursor mode not
// entered by the touchpad, or the centred cursor with click emulation under
// freelook.
bool ClicksWanted();

// A click function's edge: 4 left, 5 right (0x141277010's codes 4 / 5).
void Click(uint8_t code, bool down);
// Releases the clicks the pad holds.
void ReleaseClicks();

// The per-frame parts of 0x141264B20, around the movement and camera update:
// Begin picks the mode and reads the cursor stick, AutoDisable applies
// GamePadCursorAutoDisableSticks, End moves the cursor and applies the
// visibility.
void Begin(const State::Mapped &m, bool haveCamera);
void AutoDisable(bool moveActive, bool camActive);
void End(const State::Mapped &m, float dt, bool moveActive, bool camActive);

// Real mouse motion within 200 ms of a pad cursor move is the warp's own
// WM_MOUSEMOVE (3.80.2's mouse handler 0x14169B2B0 drops it).
bool FiltersMotion(uint32_t now);
// Real mouse motion that did not end freelook: the cursor shows when it may
// (0x14125E3B0's tail).
void OnMouseMotion();

// The cursor visibility refresh (0x141261C10).
void Refresh();
// A freelook change: the refresh, and the click assignment that follows it.
void OnFreelookChanged();

// A key binding from a pad button started spell targeting (0x1413D2AF0's
// GamePadCursorForTargeting branch).
void OnPadTargeting();

// Gamepad support stopped, or a new world / glue screen: cursor mode off,
// the cursor shown again.
void Reset();

} // namespace GamePad::Cursor
