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

// Small reads the ActionCam features share: the follow camera's world rows
// and fields, and what they ask of its target.

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"

#include <cmath>
#include <cstdint>

namespace Camera::View {

constexpr float kEpsilon = 2.3841858e-07f; // 3.80.2's 0x14415b534

// The basis rows in world space (vtable slots 1 and 2, which turn the
// transport-local basis by the transport's facing).
inline void Row(void *camera, uintptr_t slot, float out[3]) {
    using Row_t = void(__thiscall *)(void *camera, float *out);
    auto *vtable = Game::Read<uintptr_t *>(camera, 0);
    reinterpret_cast<Row_t>(vtable[slot / sizeof(uintptr_t)])(camera, out);
}

inline float TargetDistance(const void *camera) {
    return Game::Read<float>(camera, Offsets::OFF_CAMERA_TARGET_DISTANCE);
}

inline bool FarSight(const void *camera) {
    return (Game::Read<uint32_t>(camera, Offsets::OFF_CAMERA_MODE) &
            Offsets::CAMERA_MODE_FAR_SIGHT) != 0;
}

// 3.80.2's angle below horizontal (GetVectorPitchAngle 0x142a567e0).
inline float PitchAngle(float x, float y, float z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length == 0.0f)
        return 0.0f;
    const float sine = -z / length;
    return std::asin(sine < -1.0f ? -1.0f : (sine > 1.0f ? 1.0f : sine));
}

// The target as a unit, or null (3.80.2 resolves it with type mask 0x20).
inline const void *Unit(const void *target) {
    return (Object::Type(target) & Offsets::TYPEMASK_UNIT) != 0 ? target : nullptr;
}

// 3.80.2 tests the unit's mount display (+0x1267c > 0); 1.12's is
// UNIT_FIELD_MOUNTDISPLAYID.
inline bool Mounted(const void *unit) {
    const uint8_t *fields = Object::Fields(unit);
    return fields != nullptr &&
           Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_MOUNTDISPLAYID) > 0;
}

} // namespace Camera::View
