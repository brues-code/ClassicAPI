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

// The 3.80.2 camera follow (CameraFollow* cvars, FUN_141b59770): the camera
// turns to follow the character's movement. See
// docs/notes/camera-follow-3802.md.
namespace Camera::AutoFollow {

// The follow step, run right after the driver's transitions (3.80.2 step 8).
void Update(void *camera, void *target, uint32_t now, float dt);

// The end of a driver frame that built the view: keeps the target position
// the solver started from and the camera's transport, which the next frame's
// step reads (3.80.2's last-frame record, FUN_141b5ab50; the last pivot is
// the driver's +0x174).
void Record(void *camera, const float feet[3]);

} // namespace Camera::AutoFollow
