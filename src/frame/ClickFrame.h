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

namespace Frame::ClickFrame {

// 4.3.4's GetClickFrame (UIBindings.cpp), for CLICK binding commands: the
// frame named by the first `length` bytes of `name` — `_G[name]` read raw,
// a frame, and its own name equal to `name` — or null. Leaves the Lua stack
// as it found it.
void *Find(void *L, const char *name, size_t length);

} // namespace Frame::ClickFrame
