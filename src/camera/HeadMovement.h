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

// ActionCam's head movement (test_cameraHeadMovement*): the camera follows
// the character's head through a dead zone, as the 3.80.2 client does it.
// Driven by camera/Follow.cpp from 1.12's camera update.

namespace Camera::HeadMovement {

// Before the solve: pulls the dead-zone rope after the head (3.80.2 step 4),
// then eases the eye offset toward it and adds it to `offset`, the eye
// offset the solve and the eye placement use (step 11). `feet` is the
// target's position and `stabilizer` the vertical stabilizer's offset as
// last updated. `tracked` says whether the rope ran. Returns true while an
// offset is in play.
bool Apply(void *camera, const void *target, const float feet[3], float stabilizer, float dt,
           float offset[3], bool &tracked);

} // namespace Camera::HeadMovement
