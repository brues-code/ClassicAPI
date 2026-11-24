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

namespace Frame::MotionFocus {

// The frame whose enter or leave notify a change of the motion focus is
// running right now, else null. Frame::EnterLeaveMotion offers `motion` to
// exactly that frame's script.
void *Notifying();

// True when the frame or region is in the motion focus: one of the objects
// under the cursor that take motion (GetMouseFoci).
bool IsFocus(void *object);

// Takes the frame or region out of the motion focus at once, as the engine
// takes out a frame that leaves the mouse lists: it leaves with motion false,
// and the focus is picked again. Frame::MouseInput calls it when a frame's
// motion is turned off while its clicks stay on (3.80.2: OnLeave(false) at
// once), Frame::RegionInput when a region is hidden or its motion goes off.
void Drop(void *object);

// Takes an object being destroyed out of the motion focus, with no leave.
void Forget(void *object);

} // namespace Frame::MotionFocus
