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

namespace Frame::Alpha {

// A frame's own alpha byte (3.x's m_alpha, what GetAlpha returns): the last
// explicitly-set value, 255 for a frame that never set one. Presence in the
// table (not the value) is what a plate fade consults before overwriting a
// child. Plates themselves never record one; GetAlpha still reports the byte
// for the whole plate subtree.
uint8_t OwnAlpha(const void *frame);

// SetIgnoreParentAlpha / IsIgnoringParentAlpha on a frame: an ignoring frame
// draws at its own alpha, not its own times its parent's; its children
// multiply by that. Setting it redraws the frame's subtree.
void SetIgnoreParentAlpha(void *frame, bool ignore);
bool IgnoresParentAlpha(const void *frame);

} // namespace Frame::Alpha
