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

// Dynamic pitch, ported from the 3.80.2 Classic client (WowClassicGM-China.exe
// 3.80.2.69496; docs/notes/actioncam-3802.md A §3.3).
//
// 3.80.2's follow update (CGCamera_UpdateFollow 0x141b5e900) runs it in two
// steps: the blend (CGCamera_UpdateDynamicPitchBlend 0x141bc7bf0) after the
// view angles, and, after the eye is stored, the delta
// (CGCamera_ComputeDynamicPitchDelta 0x141bc7eb0), which it adds to the
// basis pitch, clamps to ±88° and turns the basis by. It turns the view only,
// at the place 1.12's driver turns the basis by its pivot bias (0x0050EE58),
// so both steps run here after 1.12's driver returns. The blend reads only
// the gate and the pad, so running it after the solver changes nothing.
//
// 1.12 differences:
//   - 3.80.2 adds the delta and the pivot bias to the pitch of a basis that
//     has neither yet. 1.12's driver has already turned the basis by the
//     bias (when at least 0.001), so the user pitch is the basis pitch less
//     that bias, and the clamp is applied to the basis pitch plus the delta.
//   - The gate's view-mode test (1 <= +0xcc <= 5) has no 1.12 field; far
//     sight, which forces the distance to 0, is the 1.12 mode it excludes.
//   - The pad target never uses BaseFovPadFlying (1.12 has no flying).
//   - The vertical stabilizer (CameraReduceUnexpectedMovement) is not
//     ported, so its term is 0.
//   - The blend's look-up zoom needs over-shoulder, which is not ported yet.

#include "camera/DynamicPitch.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/Spring.h"

#include <cmath>
#include <cstdint>

namespace Camera::DynamicPitch {

namespace {

constexpr float kEpsilon = 2.3841858e-07f;      // 0x14415b534
constexpr float kPadEpsilon = 9.5367431e-07f;   // 0x14415b544
constexpr float kPadUnset = 2.0f;               // 0x14415bf98
constexpr float kFovShare = 0.3f;               // 0x14415b9b8
constexpr float kMaxDelta = 0.08726646f;        // 0x14415b8ac, 5 degrees
constexpr float kMaxDownScale = 0.9f;           // 0x14415bc10
constexpr float kPitchLimit = 1.5358897f;       // 0x14415bdf8, 88 degrees
constexpr float kBlendOmega = 20.0f;            // 0x141bc7bf0
constexpr float kPadOmega = 12.0f;              // 0x14415c2c8
constexpr float kMinRotation = 0.001f;          // 0x14415b630, as the bias test

const float kPitchAxis[3] = {0.0f, 1.0f, 0.0f};

// 3.80.2's per-camera fields: the pad target +0x3dc, the blend +0x3e0 and
// the pad +0x3e8 (each with its velocity after it). Kept for the one world
// camera and reset when another takes its place.
struct State {
    const void *camera = nullptr;
    float padTarget = 1.0f;
    Spring blend = {0.0f, 0.0f};
    Spring pad = {kPadUnset, 0.0f};
};

State g_state;

State &For(const void *camera) {
    if (g_state.camera != camera) {
        g_state = State{};
        g_state.camera = camera;
    }
    return g_state;
}

float TargetDistance(const void *camera) {
    return Game::Read<float>(camera, Offsets::OFF_CAMERA_TARGET_DISTANCE);
}

// CGCamera_IsDynamicPitchActive 0x141b66500.
bool IsActive(const void *camera) {
    if (Settings::KeepCharacterCentered() || Settings::DynamicPitch() == 0.0f)
        return false;
    if (std::fabs(TargetDistance(camera)) < kEpsilon)
        return false;
    return (Game::Read<uint32_t>(camera, Offsets::OFF_CAMERA_MODE) &
            Offsets::CAMERA_MODE_FAR_SIGHT) == 0;
}

// CGCamera_RefreshDynamicPitchFovPad 0x141bc7e00.
void RefreshPad(State &s, const void *target) {
    s.padTarget = target != nullptr ? Settings::DynamicPitchBaseFovPad() : 1.0f;
}

// GetVectorPitchAngle 0x142a567e0: the angle below horizontal.
float PitchAngle(float x, float y, float z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length == 0.0f)
        return 0.0f;
    const float sine = -z / length;
    return std::asin(sine < -1.0f ? -1.0f : (sine > 1.0f ? 1.0f : sine));
}

// CGCamera_UpdateDynamicPitchBlend 0x141bc7bf0.
void UpdateBlend(State &s, const void *camera, const void *target, float dt) {
    bool active = IsActive(camera);
    if ((!active && std::fabs(s.blend.value) < kEpsilon) || target == nullptr) {
        s.pad = {kPadUnset, 0.0f};
        return;
    }
    if (s.padTarget <= 0.0f || 1.0f <= s.padTarget)
        active = false;
    SmoothCriticallyDamped(s.blend, active ? 1.0f : 0.0f, kBlendOmega, dt);
    if (std::fabs(s.blend.value) < kEpsilon)
        s.pad = {kPadUnset, 0.0f};
}

// CGCamera_ComputeDynamicPitchDelta 0x141bc7eb0. `height` is the solved
// pivot height and `pitch` the user pitch (positive looking down).
float Delta(State &s, const void *camera, const void *target, float height, float pitch,
            float dt) {
    bool snap = false;
    if (IsActive(camera)) {
        snap = std::fabs(s.pad.value - s.padTarget) < kPadEpsilon;
        RefreshPad(s, target);
    }
    if (std::fabs(s.blend.value) < kEpsilon || target == nullptr)
        return 0.0f;
    if (s.padTarget < 0.0f)
        s.padTarget = 1.0f;
    const float share = Game::Read<float>(camera, Offsets::OFF_CAMERA_BASE_FOV) * kFovShare;
    // 3.80.2 takes the desired distance while any of mode bits 0x70000 or
    // 0x80000 is set, else the live one. Its solver sets 0x80000 on every
    // entry (0x141b5cc1a) and only the pull-in clears it (0x141b60025), after
    // this runs, so in practice it is always the desired distance.
    const float distance = TargetDistance(camera);
    // The eye-to-feet vector: distance along the user-pitch view, then down
    // by the height. Its pitch is all that is used, so the yaw is left out.
    const float horizontal = distance * std::cos(pitch);
    const float vertical = -distance * std::sin(pitch) - height;
    const float feet = PitchAngle(horizontal, 0.0f, vertical);
    if (std::fabs(s.pad.value - kPadUnset) < kEpsilon) {
        const float top = pitch + share;
        s.pad.value = feet <= top ? (top - feet) / share : 0.0f;
    }
    if (!snap || dt + dt <= std::fabs(s.padTarget - s.pad.value))
        SmoothCriticallyDamped(s.pad, s.padTarget, kPadOmega, dt);
    else
        s.pad = {s.padTarget, 0.0f};
    float delta = kMaxDelta;
    const float edge = pitch + kMaxDelta + (1.0f - s.pad.value) * share;
    if (feet < edge)
        delta = feet - edge + kMaxDelta;
    const float downScale = Settings::DynamicPitchBaseFovPadDownScale();
    float down = downScale < 0.0f ? 0.0f : (downScale < kMaxDownScale ? downScale : kMaxDownScale);
    down *= delta;
    float limit = kPitchLimit;
    if (pitch < 0.0f) {
        limit = -kPitchLimit;
        down = 0.0f;
    }
    float t = pitch / limit;
    t = t < 0.0f ? 0.0f : (t < 1.0f ? t : 1.0f);
    const float w = (1.0f - t) * (1.0f - t);
    return (w * delta + (1.0f - w) * down) * s.blend.value;
}

} // namespace

bool DisablesSmartPivot(const void *camera) {
    return IsActive(camera) &&
           TargetDistance(camera) < Settings::DynamicPitchSmartPivotCutoffDist();
}

void Apply(void *camera, const void *target, float solvedHeight, float dt) {
    State &s = For(camera);
    float *basis = &Game::Ref<float>(camera, Offsets::OFF_CAMERA_BASIS);
    const float current = PitchAngle(basis[0], basis[1], basis[2]);
    const float bias = Game::Read<float>(camera, Offsets::OFF_CAMERA_PIVOT_BIAS);
    const float userPitch = std::fabs(bias) >= kMinRotation ? current - bias : current;

    UpdateBlend(s, camera, target, dt);
    const float delta = Delta(s, camera, target, solvedHeight, userPitch, dt);
    if (std::fabs(delta) < kMinRotation)
        return;
    float pitch = current + delta;
    if (pitch < -kPitchLimit)
        pitch = -kPitchLimit;
    else if (pitch > kPitchLimit)
        pitch = kPitchLimit;
    using RotateMatrix3_t = void(__thiscall *)(float *, float, const float *, uint8_t);
    reinterpret_cast<RotateMatrix3_t>(Offsets::FUN_ROTATE_MATRIX3)(basis, pitch - current,
                                                                   kPitchAxis, 1);
}

} // namespace Camera::DynamicPitch
