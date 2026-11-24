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

// ActionCam's over-shoulder (test_cameraOverShoulder): the pivot moves to
// the character's side, as the 3.80.2 client does it. Driven by
// camera/Follow.cpp from 1.12's camera update.

namespace Camera::OverShoulder {

// 3.80.2's CGCamera_IsOverShoulderActive.
bool IsActive(const void *camera);

// Before the solve: eases the sideways offset (3.80.2's
// CGCamera_ApplyOverShoulderOffset).
void Update(void *camera, const void *target, float dt);

// After 1.12's solve: sweeps the offset the way 3.80.2's solver does and
// moves the pivot by what is left of it, then sweeps the eye offset.
// `distanceIn` is the distance the solve started from. `sweepEyeOffset`
// asks for the eye-offset sweep even without a shoulder offset (head
// movement's offset is in it). Returns the mode bits to add to the solver's.
uint32_t Solve(void *camera, float *pivot, float *distance, const float *height,
               float *offset, float distanceIn, bool sweepEyeOffset);

// After the driver: 3.80.2's self fade while over-shoulder is on.
void Fade(void *camera, const float feet[3]);

} // namespace Camera::OverShoulder
