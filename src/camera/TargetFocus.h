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

// ActionCam's target focus (test_cameraTargetFocus*): the view turns part of
// the way toward the enemy target or the object being talked to, as the
// 3.80.2 client does it. Driven by camera/Follow.cpp after 1.12's driver.

namespace Camera::TargetFocus {

// After the driver and the pitch delta (3.80.2 step 16). `feet` is the
// target's position and `pivot` the point the camera looks at, `dt` the
// frame time in seconds.
void Apply(void *camera, const float feet[3], const float pivot[3], float dt);

} // namespace Camera::TargetFocus
