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

// The camera follow of the 3.80.2 client, FUN_141b59770
// (docs/notes/camera-follow-3802.md §3): with CameraFollowOnStick on, the
// camera turns as if a stick joined it to the character. The character
// moves, the camera stays where it was and turns to look at the new pivot,
// and its pitch eases toward 15 degrees down plus the slope of the movement.
// Both turns go through the camera's own shifts (ShiftYaw / ShiftPitch, the
// writers MouseTurn uses), so they are real camera turns, as in 3.80.2.
//
// It runs where 3.80.2 runs it: after the driver's transitions and before
// its view angles (Camera::Follow's UpdateTransitions post-hook). Its inputs
// map onto 1.12 as the note's §6.2 table gives them:
//   - last frame's pivot: the driver's own +0x174 (feet plus the solved
//     height), still last frame's at this point. 3.80.2 rebuilds it from the
//     last target position plus a pivot offset; 1.12 keeps it whole;
//   - last frame's target position: ours (Record), the feet the solver
//     started from;
//   - last frame's eye +0x08 and forward row, rebuilt later this frame;
//   - the manual-adjust stamp: 3.80.2 stamps it in ApplyMouseLook, every
//     camera turn's entry; 1.12's MouseTurn keeps no time, so its co-hook
//     here stamps it. Mouse look and the gamepad camera stick both turn
//     through MouseTurn.
//   - gamepad freelook: GamePad::Movement::FreelookOn (3.80.2's input flag
//     0x2000000).
//
// Not yet ported (the note's §3.3 and the facing half of §3.6): following
// the locked target (CameraFollowTarget, its combat and PvP tests) and
// turning or snapping the character. 3.80.2 runs that half only while the
// camera yaw is relative to the character (absolute-frame counter 0); under
// gamepad freelook the counter is raised, so there 3.80.2 does what this
// does: the whole turn goes to the camera. On a transport the step rests;
// 3.80.2 works in the transport's frame, which is not worked out for 1.12.

#include "camera/AutoFollow.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/View.h"
#include "gamepad/Movement.h"
#include "time/Clock.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>

namespace Camera::AutoFollow {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kEps = View::kEpsilon;
constexpr float kMoveEpsSq = 0.0001f;       // 3.80.2's per-frame movement threshold
constexpr float kEaseInMin = 4.7683716e-07f; // below it the ease-in is instant
constexpr float kInvPitchLimit = 0.6510884f; // 1 / 88 degrees: steep views weaken the yaw turn

struct State {
    void *camera = nullptr;
    bool hasRecord = false;
    float lastFeet[3] = {};     // last frame's target position
    float smoothed[3] = {};     // smoothed movement (rate 5/s)
    float ease = 1.0f;
    bool initialized = false;
    bool resuming = false;      // inside the adjust delay
    uint32_t seen = 0;          // last stamp this step saw
};

State g_state;

// The manual-adjust stamp: the time of the last MouseTurn.
bool g_hasStamp = false;
uint32_t g_stamp = 0;

State &For(void *camera) {
    if (g_state.camera != camera) {
        g_state = State{};
        g_state.camera = camera;
    }
    return g_state;
}

float WrapPi(float a) {
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.0f)
        a += kTwoPi;
    return a - kPi;
}

float Norm2Pi(float a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0f ? a + kTwoPi : a;
}

// FUN_142a5c180: goal - exp(-rate dt) (goal - cur).
float Approach(float cur, float goal, float rate, float dt) {
    return goal - std::exp(-rate * dt) * (goal - cur);
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void Reset(State &s) {
    s.smoothed[0] = s.smoothed[1] = s.smoothed[2] = 0.0f;
    s.ease = 1.0f;
    s.initialized = false;
    s.resuming = false;
}

void ShiftYaw(void *camera, float delta) {
    using Shift_t = void(__thiscall *)(void *camera, float delta);
    reinterpret_cast<Shift_t>(Offsets::FUN_CAMERA_SHIFT_YAW)(camera, delta);
}

void ShiftPitch(void *camera, float delta) {
    using Shift_t = void(__thiscall *)(void *camera, float delta);
    reinterpret_cast<Shift_t>(Offsets::FUN_CAMERA_SHIFT_PITCH)(camera, delta);
}

// §3.1: every case that resets the step.
bool Gated(const void *camera, const State &s) {
    if (!Settings::FollowOnStick())
        return true;
    const uint32_t mode = Game::Read<uint32_t>(camera, Offsets::OFF_CAMERA_MODE);
    if ((mode & Offsets::CAMERA_MODE_MOUSELOOK) != 0 && !GamePad::Movement::FreelookOn())
        return true;
    if ((mode & (Offsets::CAMERA_MODE_YAW_TRANSITION | Offsets::CAMERA_MODE_PITCH_TRANSITION |
                 Offsets::CAMERA_MODE_FAR_SIGHT)) != 0)
        return true;
    if (std::fabs(View::TargetDistance(camera)) < kEps)
        return true;
    if (!s.hasRecord)
        return true;
    return Game::Read<uint64_t>(camera, Offsets::OFF_CAMERA_TRANSPORT_GUID) != 0;
}

// §3.6, the camera half.
void FollowYaw(void *camera, State &s, const float move[3], const float toPivot[3]) {
    const float yawSpeed = Settings::FollowYawSpeed();
    if (yawSpeed <= 0.0f || move[0] * move[0] + move[1] * move[1] <= kMoveEpsSq)
        return;
    float forward[3];
    View::Row(camera, Offsets::VTBL_CAMERA_FORWARD, forward);
    const float length = std::sqrt(Dot3(move, move));
    const float dir[3] = {move[0] / length, move[1] / length, move[2] / length};
    const float along = std::fabs(Dot3(forward, dir));
    const float k = Clamp((1.5f * (1.0f - along) + 4.0f * along) * yawSpeed, 0.0f, 100.0f);
    const float p[3] = {toPivot[0] + move[0] * k, toPivot[1] + move[1] * k, toPivot[2] + move[2] * k};
    const float steep = std::fmin(std::fabs(View::PitchAngle(p[0], p[1], p[2])) * kInvPitchLimit, 1.0f);
    float delta = WrapPi(Norm2Pi(std::atan2(p[1], p[0])) - Norm2Pi(std::atan2(toPivot[1], toPivot[0])));
    delta *= (1.0f - steep) * s.ease;
    if (s.resuming && std::fabs(s.ease - 1.0f) < kEps && std::fabs(delta) < kEps)
        s.resuming = false;
    if (std::fabs(s.ease) < kEps)
        return;
    if (std::fabs(delta) >= kEps)
        ShiftYaw(camera, delta);
}

// §3.7.
void FollowPitch(void *camera, const void *target, const State &s, const float toPivot[3], float dt) {
    float sm[3] = {s.smoothed[0], s.smoothed[1], s.smoothed[2]};
    if (Settings::ReduceUnexpectedMovement())
        sm[2] = 0.0f;
    const float r0 = Settings::FollowPitchSpeed() * s.ease;
    const void *unit = View::Unit(target);
    if (r0 <= 0.0f || Dot3(sm, sm) <= kMoveEpsSq || unit == nullptr)
        return;
    void *movement = Game::Read<void *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    if (movement != nullptr &&
        (Game::Read<uint32_t>(movement, Offsets::OFF_MOVEMENT_FLAGS) & Offsets::MOVEFLAG_FALLING) != 0) {
        using FallSpeed_t = float(__thiscall *)(void *);
        const float fallSpeed =
            reinterpret_cast<FallSpeed_t>(Offsets::FUN_CMOVEMENT_GET_CURRENT_FALL_SPEED)(movement);
        if (fallSpeed <= Game::Read<float>(Offsets::VAR_MOVER_TERMINAL_SPEED))
            return; // off while falling
    }
    // Rising toward the camera looks down less, as 3.80.2 flips the slope.
    const float sign = (sm[2] > 0.0f && sm[0] * toPivot[0] + sm[1] * toPivot[1] < 0.0f) ? -1.0f : 1.0f;
    const float minPitch = Game::Read<float>(Offsets::VAR_CAMERA_MIN_PITCH);
    const float maxPitch = Game::Read<float>(Offsets::VAR_CAMERA_MAX_PITCH);
    float goal = View::PitchAngle(sm[0], sm[1], sm[2]) * Clamp(Settings::FollowPitchStrength(), 0.0f, 1.0f) *
                     sign +
                 Settings::FollowPitchOffset() * kDegToRad;
    goal = Clamp(goal, std::fmin(minPitch, maxPitch), std::fmax(minPitch, maxPitch));
    const float cur = Game::Read<float>(camera, Offsets::OFF_CAMERA_PITCH) +
                      Game::Read<float>(camera, Offsets::OFF_CAMERA_PIVOT_BIAS);
    const float error = WrapPi(goal - cur);
    const float deadZone = Clamp(Settings::FollowPitchDeadZone(), 0.0f, 180.0f) * kDegToRad;
    if (error >= -deadZone && error <= deadZone)
        return;
    const float dist = Clamp(View::TargetDistance(camera), 0.0f, 50.0f);
    const float k = (error > 0.0f && goal > 0.0f) ? 4.0f : 3.0f;
    const float rate = std::fmax(k * (1.0f - dist * 0.015f) * r0, 0.0f);
    ShiftPitch(camera, WrapPi(Approach(cur, goal, rate, dt) - cur));
}

using MouseTurn_t = void(__fastcall *)(void *camera, void *edx, float dx, float dy);
MouseTurn_t MouseTurn_o = nullptr;

void __fastcall MouseTurn_h(void *camera, void *edx, float dx, float dy) {
    g_hasStamp = true;
    g_stamp = Time::Clock::NowMs();
    MouseTurn_o(camera, edx, dx, dy);
}

const Game::HookAutoRegister _mouseTurnHook{Offsets::FUN_CAMERA_MOUSE_TURN,
                                            reinterpret_cast<void *>(&MouseTurn_h),
                                            reinterpret_cast<void **>(&MouseTurn_o)};


} // namespace

void Update(void *camera, void *target, uint32_t now, float dt) {
    State &s = For(camera);
    if (target == nullptr || Gated(camera, s)) {
        Reset(s);
        return;
    }

    const float *last = s.lastFeet;
    float position[3];
    if (!Unit::Position::Read(target, position))
        return;
    // §3.4, on stick: this frame's move, scaled down closer than 4 yards.
    const float w = std::fmin(View::TargetDistance(camera) * 0.25f, 1.0f);
    const float move[3] = {(position[0] - last[0]) * w, (position[1] - last[1]) * w,
                           (position[2] - last[2]) * w};

    // §3.5: the adjust delay and the ease back in.
    const uint32_t delayMs = static_cast<uint32_t>(Settings::FollowAdjustDelay() * 1000.0f);
    const bool freelook = GamePad::Movement::FreelookOn();
    bool inDelay = false;
    if (g_hasStamp) {
        if (g_stamp != s.seen) {
            if (freelook && !Time::Clock::Reached(now, g_stamp + delayMs))
                s.ease = 0.0f;
            s.seen = g_stamp;
        }
        inDelay = !Time::Clock::Reached(now, g_stamp + delayMs);
    }
    if (inDelay) {
        if (!s.initialized)
            s.resuming = true;
    } else if (s.ease < 1.0f) {
        const float easeIn = Settings::FollowAdjustEaseIn();
        s.ease = easeIn <= kEaseInMin ? 1.0f : Clamp(s.ease + dt / easeIn, 0.0f, 1.0f);
    }
    if (!s.initialized) {
        s.initialized = true;
        s.smoothed[0] = move[0];
        s.smoothed[1] = move[1];
        s.smoothed[2] = move[2];
        return;
    }
    const float keep = std::exp(-5.0f * dt);
    for (int i = 0; i < 3; ++i)
        s.smoothed[i] = move[i] - (move[i] - s.smoothed[i]) * keep;
    if (Dot3(move, move) <= kMoveEpsSq) {
        s.resuming = false;
        return;
    }

    // Camera to pivot, last frame.
    const float *pivot = &Game::Ref<float>(camera, Offsets::OFF_CAMERA_LAST_PIVOT);
    const float *eye = &Game::Ref<float>(camera, Offsets::OFF_CAMERA_POSITION);
    const float toPivot[3] = {pivot[0] - eye[0], pivot[1] - eye[1], pivot[2] - eye[2]};
    FollowYaw(camera, s, move, toPivot);
    FollowPitch(camera, target, s, toPivot, dt);
}

void Record(void *camera, const float feet[3]) {
    State &s = For(camera);
    s.lastFeet[0] = feet[0];
    s.lastFeet[1] = feet[1];
    s.lastFeet[2] = feet[2];
    s.hasRecord = true;
}

} // namespace Camera::AutoFollow
