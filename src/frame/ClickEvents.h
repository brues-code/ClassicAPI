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

#include <cstddef>
#include <cstdint>

namespace Frame::ClickEvents {

// The button name of the innermost click / mouse dispatch running now (one of
// the engine's static name literals), or null outside one — what
// GetMouseButtonClicked returns; 4.3.4's CSimpleTop::m_mouseButtonName.
const char *CurrentButton();

// The engine's own name literal for a mouse button bit (left 1, middle 2,
// right 4, X1 8, X2 0x10) — the pointer FUN_BUTTON_CLICK hands OnClick;
// "UNKNOWN" for any other mask.
const char *ButtonNameForMask(uint32_t mask);

// A mouse button's name as the later clients read one (3.80.2's
// FUN_140597C00): LeftButton or Button1, RightButton or Button2, MiddleButton
// or Button3, Button4 to Button31, ignoring case; the first `len` characters
// of `name`. True for a button name, with `mask` its bit, 0 for a button 1.12
// has no bit for (Button6 and up).
bool ParseButtonName(const char *name, size_t len, uint32_t &mask);

// The bit for a button name, as ParseButtonName reads it; 0 for any other
// name.
uint32_t MaskForButtonName(const char *name);

} // namespace Frame::ClickEvents
