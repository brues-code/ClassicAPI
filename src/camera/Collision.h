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

// The parts of 3.80.2's camera solver (CGCamera_SolveCollision 0x141b5cbc0)
// that 1.12's SolveLimits lacks, run on its result: the over-shoulder
// sweeps, indirect visibility and the eye-offset sweep.

namespace Camera::Collision {

// After 1.12's solve. `pivot` (the feet; moved by what is left of the
// shoulder offset), `distance` and `offset` are the driver's own; `height`
// is the solved pivot height; `distanceIn` the distance the solve started
// from; `shoulder` the over-shoulder offset in world space;
// `sweepEyeOffset` asks for the eye-offset sweep even without a shoulder
// offset; `bits` the mode bits 1.12's solve returned. Returns the bits to
// return in their place.
uint32_t Solve(void *camera, const void *target, float *pivot, float *distance,
               const float *height, float *offset, float distanceIn, const float shoulder[3],
               bool sweepEyeOffset, uint32_t bits);

} // namespace Camera::Collision
