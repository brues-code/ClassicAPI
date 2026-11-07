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

// A camera's properties (Offsets::CAMERA_PROP_*): vectors (eye, target) and
// scalars (clips, field of view, roll, yaw).
Vec3 GetVector(void *camera, uint32_t id);
void SetVector(void *camera, uint32_t id, const Vec3 &v);
float GetScalar(void *camera, uint32_t id);
void SetScalar(void *camera, uint32_t id, float value);

} // namespace Model::Camera
