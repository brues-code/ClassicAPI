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

// The vertical stabilizer, ported from the 3.80.2 Classic client
// (WowClassicGM-China.exe 3.80.2.69496, CGCamera_UpdateVerticalStabilizer
// 0x141b5a680, called from its transitions 0x141b60ff1).
//
// While it is on, each frame's vertical move of the target is taken off the
// offset (+0x478) that 3.80.2 adds to the pivot height, so the camera lags
// it; every frame the offset decays as e^(−k·dt) less a minimum step, and is
// held within limits. The rates and limits depend on the live distance (s =
// clamp((distance − 1)/14), from close to 15 yards), with a slower decay
// while the target is still (×12, and an 8× larger minimum step while the
// distance is also steady) and a faster one while it moves (×21). In first
// person the limits are ±0.3 yards. With cameraZSmooth 1 the offset is held
// while the target and the distance are still, the distance is over 2.5
// yards and head movement did not run; 2 keeps smoothing then too.
//
// It is on when the character is not kept centered,
// CameraReduceUnexpectedMovement is 1, cameraZSmooth is nonzero, and the
// camera is not in far sight; and the target is not falling faster than an
// eighth of the terminal velocity, on a taxi, swimming, jumping (falling
// with a start speed) or on a spline. Otherwise the offset decays to 0 at the
// default rate (k = 3, minimum step 0.1 yd/s, limits ±2).
//
// 1.12 pieces:
//   - The fall speed is CMovement::GetCurrentFallSpeed 0x007C5D70 (the same
//     model as 3.80.2's FUN_140f3e7c0) against VAR_MOVER_TERMINAL_SPEED.
//   - Swimming is MOVEFLAG_SWIMMING (retail 0x100000; retail's flying
//     0x1000000 has no 1.12 counterpart). On a taxi is UNIT_FLAG_TAXI_FLIGHT.
//     The spline is CMovement +0xA4: 3.80.2's +0x2b0 is the pointer its
//     start-flying test (FUN_140f40880) also refuses on [inf: the move
//     spline].
//   - 3.80.2 also turns it off in its character customization mode
//     (DAT_144dd3829, cleared by the barber shop exit FUN_1416e0f00), which
//     1.12 does not have.
//   - The offset reaches the pivot by moving the pivot's z before 1.12's
//     solve (3.80.2 adds it to the height before its solve), so the ceiling
//     probe starts from the moved point.

#include "camera/Stabilizer.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/View.h"
#include "object/Resolve.h"

#include <cmath>
#include <cstdint>

namespace Camera::Stabilizer {

namespace {

constexpr float kStillSq = 2.5e-05f;
constexpr float kSteady = 0.005f;
constexpr float kHoldDistance = 2.5f;
constexpr float kFallShare = 0.125f;

// 3.80.2's +0x468 (last target position), +0x474 (last live distance),
// +0x478 (the offset) and +0x47c (on last frame).
struct State {
    const void *camera = nullptr;
    float last[3] = {0.0f, 0.0f, 0.0f};
    float lastDistance = 0.0f;
    float offset = 0.0f;
    bool wasOn = false;
};

State g_state;

State &For(const void *camera) {
    if (g_state.camera != camera) {
        g_state = State{};
        g_state.camera = camera;
    }
    return g_state;
}

// The movement tests of 0x141b5a680 (0x141b5a7a0..0x141b5a813).
bool MovementAllows(const void *unit) {
    const uint8_t *fields = Object::Fields(unit);
    if (fields != nullptr && (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
                              Offsets::UNIT_FLAG_TAXI_FLIGHT) != 0)
        return false;
    void *movement = Game::Read<void *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    if (movement == nullptr)
        return true;
    using FallSpeed_t = float(__thiscall *)(void *);
    const float fallSpeed =
        reinterpret_cast<FallSpeed_t>(Offsets::FUN_CMOVEMENT_GET_CURRENT_FALL_SPEED)(movement);
    if (Game::Read<float>(Offsets::VAR_MOVER_TERMINAL_SPEED) * kFallShare < fallSpeed)
        return false;
    const uint32_t flags = Game::Read<uint32_t>(movement, Offsets::OFF_MOVEMENT_FLAGS);
    if ((flags & Offsets::MOVEFLAG_SWIMMING) != 0)
        return false;
    if ((flags & Offsets::MOVEFLAG_FALLING) != 0 &&
        Game::Read<float>(movement, Offsets::OFF_MOVEMENT_FALL_START_SPEED) != 0.0f)
        return false;
    return Game::Read<void *>(movement, Offsets::OFF_MOVEMENT_SPLINE) == nullptr;
}

bool On(const void *camera, const void *target) {
    if (Settings::KeepCharacterCentered() || !Settings::ReduceUnexpectedMovement())
        return false;
    if (Settings::ZSmooth() == 0 || View::FarSight(camera))
        return false;
    const void *unit = View::Unit(target);
    return unit == nullptr || MovementAllows(unit);
}

} // namespace

float Offset(const void *camera) { return For(camera).offset; }

float Update(void *camera, const void *target, const float feet[3], bool headTracked, float dt) {
    State &s = For(camera);
    float k = 3.0f;
    float minStep = 0.1f;
    float low = -2.0f;
    float high = 2.0f;
    bool on = target != nullptr && On(camera, target);
    const float live = Game::Read<float>(camera, Offsets::OFF_CAMERA_DISTANCE);
    if (on) {
        const float dx = feet[0] - s.last[0];
        const float dy = feet[1] - s.last[1];
        const float dz = feet[2] - s.last[2];
        const bool still = dx * dx + dy * dy + dz * dz <= kStillSq;
        const bool steady = std::fabs(live - s.lastDistance) < kSteady;
        const bool thirdPerson = std::fabs(View::TargetDistance(camera)) >= View::kEpsilon;
        if (thirdPerson && !headTracked && still && steady && kHoldDistance < live &&
            Settings::ZSmooth() < 2) {
            on = false;
            k = 0.0f;
        } else if (s.wasOn) {
            if (thirdPerson) {
                float near = (live - 1.0f) * 0.071428575f;
                near = near < 0.0f ? 0.0f : (near < 1.0f ? near : 1.0f);
                const float far = 1.0f - near;
                k = near * 0.8f + far;
                high = far * 0.3f + near;
                low = near * -1.5f - far * 0.45f;
                minStep = near * 0.1f + far * 0.05f;
            } else {
                low = -0.3f;
                high = 0.3f;
                k = 1.5f;
                minStep = 0.1f;
            }
            if (still) {
                k *= 12.0f;
                if (steady)
                    minStep *= 8.0f;
            } else {
                k *= 21.0f;
            }
            s.offset -= dz;
        }
    }
    s.wasOn = on;
    if (View::kEpsilon <= std::fabs(k)) {
        const float sign = s.offset > 0.0f ? 1.0f : (s.offset < 0.0f ? -1.0f : 0.0f);
        const float decayed = std::exp(-(k * dt)) * s.offset;
        float next = 0.0f;
        if (minStep * dt < std::fabs(decayed))
            next = decayed - sign * minStep * dt;
        s.offset = next < low ? low : (next > high ? high : next);
    }
    s.last[0] = feet[0];
    s.last[1] = feet[1];
    s.last[2] = feet[2];
    s.lastDistance = live;
    return s.offset;
}

} // namespace Camera::Stabilizer
