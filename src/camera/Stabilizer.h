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

// The vertical stabilizer (CameraReduceUnexpectedMovement with cameraZSmooth):
// the pivot lags the character's vertical moves and eases back, as the 3.80.2
// client does it. Driven by camera/Follow.cpp from 1.12's camera update.

namespace Camera::Stabilizer {

// The offset 3.80.2 adds to the pivot height (+0x478), as last updated.
float Offset(const void *camera);

// Once per frame, before the solve (3.80.2 runs it inside its transitions).
// `feet` is the target's position, `headTracked` whether head movement's
// rope ran this frame, `dt` the frame time in seconds. Returns the new
// offset.
float Update(void *camera, const void *target, const float feet[3], bool headTracked, float dt);

} // namespace Camera::Stabilizer
