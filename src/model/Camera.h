// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

#include <cstdint>

namespace Model::Camera {

struct Vec3 {
    float x, y, z;
};

// The M2 model's camera with id `id` (0 = portrait), or null when it has none.
void *CameraById(void *model, uint32_t id);

// A camera's vector property (Offsets::CAMERA_PROP_EYE / _TARGET).
Vec3 GetVector(void *camera, uint32_t id);

} // namespace Model::Camera
