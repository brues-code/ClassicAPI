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

// Over-shoulder, ported from the 3.80.2 Classic client (WowClassicGM-China.exe
// 3.80.2.69496; docs/notes/actioncam-3802.md A §3.4).
//
// 3.80.2 eases a sideways offset (CGCamera_ApplyOverShoulderOffset
// 0x141bc81b0: toward −(object scale × cvar) along the basis's left row,
// stiffness 30; 0 while you target yourself, a tenth while mounted) and
// hands it to its solver, whose sweeps of it are camera/Collision.cpp.
//
// 1.12 differences:
//   - The self fade keeps 1.12's threshold (1.8315) and smoothstep, with
//     3.80.2's over-shoulder changes: the distance is from the eye to the
//     character, not along the boom, and the threshold is
//     threshold·2.5·s + 0.1·(1 − s) with s = clamp((pitch + 60°)·−1/28°,
//     0, 1) (0x141b603d8..0x141b60443).
//   - The view-mode test (1 <= +0xcc <= 5) is far sight in 1.12, as for
//     dynamic pitch.

#include "camera/OverShoulder.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/Spring.h"
#include "camera/View.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cmath>

namespace Camera::OverShoulder {

namespace {

constexpr float kOmega = 30.0f;           // 0x41f00000 at 0x141bc8300
constexpr float kMountedShare = 0.1f;     // 0x141bc82d9
constexpr float kFadePitch = -1.0471976f;    // 0x14415c8f4, -60 degrees
constexpr float kFadePitchScale = -2.046278f; // 0x14415c94c, -1/28 degrees
constexpr float kFadeFar = 2.5f;             // 0x14415c02c
constexpr float kFadeNear = 0.1f;            // 0x14415b8c4
constexpr float kPi = 3.14159265f;

// 3.80.2's +0x3f0 (and its velocity), for the one world camera; and this
// frame's offset in world space.
struct State {
    const void *camera = nullptr;
    Spring current = {0.0f, 0.0f};
    float offset[3] = {0.0f, 0.0f, 0.0f};
};

State g_state;

State &For(const void *camera) {
    if (g_state.camera != camera) {
        g_state = State{};
        g_state.camera = camera;
    }
    return g_state;
}

} // namespace

bool IsActive(const void *camera) {
    if (Settings::KeepCharacterCentered() || Settings::ReduceUnexpectedMovement() ||
        Settings::OverShoulder() == 0.0f)
        return false;
    return std::fabs(View::TargetDistance(camera)) >= View::kEpsilon && !View::FarSight(camera);
}

void Update(void *camera, const void *target, float dt) {
    State &s = For(camera);
    s.offset[0] = s.offset[1] = s.offset[2] = 0.0f;
    const bool active = IsActive(camera);
    if (!active && std::fabs(s.current.value) < View::kEpsilon)
        return;
    if (target == nullptr)
        return;
    float shoulder = active ? Settings::OverShoulder() : 0.0f;
    if (shoulder > 0.0f) {
        const uint64_t player = Unit::Identity::PlayerGuid();
        if (Object::Guid(target) == player &&
            Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO) == player) {
            shoulder = 0.0f;
        } else {
            const void *unit = View::Unit(target);
            if (unit != nullptr && View::Mounted(unit))
                shoulder *= kMountedShare;
        }
    }
    if (std::fabs(shoulder) >= View::kEpsilon || std::fabs(s.current.value) >= View::kEpsilon) {
        const uint8_t *data = Object::Data(target);
        const float scale =
            data != nullptr ? Game::Read<float>(data, Offsets::OFF_OBJECT_FIELD_SCALE_X) : 0.0f;
        SmoothCriticallyDamped(s.current, -(scale * shoulder), kOmega, dt);
    }
    float left[3];
    View::Row(camera, Offsets::VTBL_CAMERA_LEFT, left);
    for (int i = 0; i < 3; ++i)
        s.offset[i] = s.current.value * left[i];
}

void Offset(const void *camera, float out[3]) {
    const State &s = For(camera);
    out[0] = s.offset[0];
    out[1] = s.offset[1];
    out[2] = s.offset[2];
}

void Fade(void *camera, const float feet[3]) {
    if (!IsActive(camera))
        return;
    const float *eye = Game::Ptr<float>(camera, Offsets::OFF_CAMERA_POSITION);
    const float dx = eye[0] - feet[0];
    const float dy = eye[1] - feet[1];
    const float dz = eye[2] - feet[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz) -
                    Game::Read<float>(camera, Offsets::OFF_CAMERA_NEAR_CLIP);
    float s = (Game::Read<float>(camera, Offsets::OFF_CAMERA_PITCH) - kFadePitch) * kFadePitchScale;
    s = s < 0.0f ? 0.0f : (s < 1.0f ? s : 1.0f);
    const float threshold = Game::Read<float>(Offsets::VAR_CAMERA_NEAR_DISTANCE) * kFadeFar * s +
                            kFadeNear * (1.0f - s);
    uint32_t alpha = 255;
    if (d < threshold) {
        if (d <= Game::Read<float>(Offsets::VAR_CAMERA_FADE_HIDDEN))
            alpha = 0;
        else
            alpha = static_cast<uint32_t>((1.0f - std::cos(kPi * d / threshold)) * 0.5f * 255.0f);
    }
    using SetTargetAlpha_t = void(__thiscall *)(void *, uint32_t);
    reinterpret_cast<SetTargetAlpha_t>(Offsets::FUN_CAMERA_SET_TARGET_ALPHA)(camera, alpha);
}

} // namespace Camera::OverShoulder
