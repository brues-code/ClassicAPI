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

namespace Frame::MouseRegistration {

// The click-mask bits for one click name ("LeftButtonUp", "AnyDown", ...),
// matched case-insensitively; 0 for an unknown name. 3.3.5's
// MouseButtonStringToBitmask.
uint32_t MaskForName(const char *name);

// Whether `frame`'s OnMouseDown (`down`) or OnMouseUp fire for the mouse
// button `name` (one of the engine's name literals, as the fire passes it)
// may run under the frame's `RegisterForMouse` registration. True for every
// frame that never called it — the AnyUp, AnyDown default.
bool Accepts(void *frame, bool down, const char *name);

} // namespace Frame::MouseRegistration
