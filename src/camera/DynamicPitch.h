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

// ActionCam's dynamic pitch (test_cameraDynamicPitch): an extra view pitch
// that keeps the character's feet at a fraction of the screen, as the 3.80.2
// client does it. Driven by camera/Follow.cpp from 1.12's camera update.

namespace Camera::DynamicPitch {

// 3.80.2's CGCamera_IsDynamicPitchActive and the test GetSmartPivotFlags
// makes with it: true when dynamic pitch turns the smart pivot off.
bool DisablesSmartPivot(const void *camera);

// Runs after the driver has built the eye and the basis: eases the blend
// and the screen pad, then turns the basis by the pitch delta. `target` is
// the camera's target, `solvedHeight` the pivot height the solver gave this
// frame, `dt` the frame time in seconds.
void Apply(void *camera, const void *target, float solvedHeight, float dt);

} // namespace Camera::DynamicPitch
