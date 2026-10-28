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

namespace Frame::MouseInput {

// True when the frame takes mouse motion: its mouse input is on and its
// motion half is not switched off.
bool MotionEnabled(void *frame);

// The input removal took the frame out of the mouse lists (hidden, mouse
// disabled, detached): it gets no button-up from a press it shared.
void OnInputRemoved(void *frame);

} // namespace Frame::MouseInput
