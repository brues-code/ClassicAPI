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

namespace Input::Modifier {

// The cached left/right modifier bitmap the `Is*KeyDown` globals read (bit
// 0 LSHIFT, 1 RSHIFT, 2 LCTRL, 3 RCTRL, 4 LALT, 5 RALT). Maintained by the
// thread message hook in Modifier.cpp, so it changes exactly when
// `MODIFIER_STATE_CHANGED` fires. C++ pollers (the `#showtooltip`
// evaluator) compare it across ticks to re-evaluate `[mod:...]`
// conditionals the frame a modifier changes, instead of waiting for the
// periodic pass.
uint32_t CurrentMask();

} // namespace Input::Modifier
