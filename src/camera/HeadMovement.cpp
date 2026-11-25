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

// Head movement, ported from the 3.80.2 Classic client (WowClassicGM-China.exe
// 3.80.2.69496; docs/notes/actioncam-3802.md A §3.5).
//
// Two steps of 3.80.2's follow update (CGCamera_UpdateFollow 0x141b5e900):
//   - The rope (0x141b5ec26..0x141b5ee2e), only while the camera follows
//     the active player. The head is attachment 55, attachment 17 or their
//     midpoint, taken relative to the model's origin and, in z, to the
//     pivot height plus the vertical stabilizer's offset. Each axis of the
//     rope (+0x1b4) follows it once it is more than the dead zone away.
//     When the pivot height then moves on a frame the rope ran, its z
//     takes the change back (old − new, 0x141b60fca), so a height ease
//     does not pull the rope.
//   - The eye offset (0x141b5f58d..0x141b5f6e5). The goal is strength ×
//     rope (CGCamera_GetHeadMovementStrength 0x141b63aa0, gated by
//     CGCamera_IsHeadMovementActive 0x141b62b50). The offset (+0x178) moves
//     to it as goal − (goal − offset)·e^(−rate·dt), at the first-person,
//     moving or standing damp rate, and is added to the eye offset, so the
//     eye and the point it looks at move together.
//
// 1.12 pieces:
//   - The head is CM2Model::GetAttachmentPosition 0x00712D50 on the body
//     model (+0xD8). It runs before this frame's scene collect pass
//     (0x00707680, which advances the frame counter), so its EnsureAnimated
//     is a no-op and the point is last frame's pose in world space. 1.12
//     models have no attachment 55 (the lookup is bounds-checked), so it is
//     attachment 17 alone.
//   - The origin is the target's position, where both its models stand.
//   - "Moving" is the camera's movement-command bit (+0x90 0x200, the same
//     bit as 3.80.2's); falling is MOVEFLAG_FALLING; on a taxi is
//     UNIT_FLAG_TAXI_FLIGHT; mounted is a mount model (+0xDC).
//   - 3.80.2 caches the dead zone when it rebuilds the pivot heights; this
//     reads the cvar each frame.
//   - Both steps run at the head of 1.12's solve, after its transitions, so
//     the height compensation comes first: the rope's z takes the change
//     since last frame's height, then the rope runs on this frame's. That
//     is 3.80.2's order seen from the next frame.

#include "camera/HeadMovement.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/View.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cmath>
#include <cstdint>

namespace Camera::HeadMovement {

namespace {

constexpr uint32_t kHeadAttachment = 0x37;   // 55, 0x141b5ec60
constexpr uint32_t kPivotAttachment = 0x11;  // 17, 0x141b5ec6d
constexpr float kMaxScale = 1.5f;
constexpr float kMaxSpan = 30.0f;
constexpr float kMountedShare = 0.5f;

struct State {
    const void *camera = nullptr;
    float rope[3] = {0.0f, 0.0f, 0.0f};   // 3.80.2's +0x1b4
    float offset[3] = {0.0f, 0.0f, 0.0f}; // 3.80.2's +0x178
    bool hasHeight = false;
    float height = 0.0f;                  // the pivot height last frame
};

State g_state;

State &For(const void *camera) {
    if (g_state.camera != camera) {
        g_state = State{};
        g_state.camera = camera;
    }
    return g_state;
}

float Scale(const void *unit) {
    const uint8_t *data = Object::Data(unit);
    const float scale =
        data != nullptr ? Game::Read<float>(data, Offsets::OFF_OBJECT_FIELD_SCALE_X) : 0.0f;
    return scale < kMaxScale ? scale : kMaxScale;
}

float LiveDistance(const void *camera) {
    return Game::Read<float>(camera, Offsets::OFF_CAMERA_DISTANCE);
}

// The range the strength fades over: from r to r + span yards.
void Range(float scale, float &r, float &span) {
    r = scale * Settings::HeadMovementRangeScale();
    span = r + r;
    if (scale * kMaxSpan <= span)
        span = scale * kMaxSpan;
}

bool Moving(const void *camera) {
    return (Game::Read<uint32_t>(camera, Offsets::OFF_CAMERA_MODE) &
            Offsets::CAMERA_MODE_MOVE_COMMAND) != 0;
}

// CGCamera_IsHeadMovementActive 0x141b62b50.
bool IsActive(const void *camera, const void *unit) {
    if (unit == nullptr || View::FarSight(camera))
        return false;
    if (!(0.0f < Settings::HeadMovementStrength()) || Settings::KeepCharacterCentered())
        return false;
    float r, span;
    Range(Scale(unit), r, span);
    if (r + span < LiveDistance(camera))
        return false;
    const uint8_t *fields = Object::Fields(unit);
    return fields == nullptr ||
           (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
            Offsets::UNIT_FLAG_TAXI_FLIGHT) == 0;
}

// CGCamera_GetHeadMovementStrength 0x141b63aa0.
float Strength(const void *camera, const void *unit) {
    const bool moving = Moving(camera);
    const void *movement = Game::Read<void *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    const bool falling = movement != nullptr &&
                         (Game::Read<uint32_t>(movement, Offsets::OFF_MOVEMENT_FLAGS) &
                          Offsets::MOVEFLAG_FALLING) != 0;
    if (std::fabs(Settings::HeadMovementStandingStrength()) < View::kEpsilon && !moving &&
        !falling)
        return 0.0f;
    float r, span;
    Range(Scale(unit), r, span);
    float t = (LiveDistance(camera) - r) / span;
    t = t < 0.0f ? 0.0f : (t < 1.0f ? t : 1.0f);
    float base = Settings::HeadMovementStrength();
    float k = moving ? Settings::HeadMovementMovingStrength()
                     : Settings::HeadMovementStandingStrength();
    if (std::fabs(View::TargetDistance(camera)) < View::kEpsilon)
        base += base;
    k *= base;
    if (Game::Read<void *>(unit, Offsets::OFF_CGOBJECT_OVERRIDE_MODEL) != nullptr)
        k *= kMountedShare;
    k = k < 0.0f ? 0.0f : (k < 1.0f ? k : 1.0f);
    return (1.0f - t) * k;
}

bool HasAttachment(void *model, uint32_t id) {
    using HasAttachment_t = int(__thiscall *)(void *, uint32_t);
    return reinterpret_cast<HasAttachment_t>(Offsets::FUN_M2MODEL_HAS_ATTACHMENT)(model, id) != 0;
}

void AttachmentPosition(void *model, uint32_t id, float out[3]) {
    using Position_t = float *(__thiscall *)(void *, float *, uint32_t);
    reinterpret_cast<Position_t>(Offsets::FUN_M2MODEL_GET_ATTACHMENT_POSITION)(model, out, id);
}

// The head-tracking block of 0x141b5e900. True when the rope ran.
bool Track(State &s, const void *camera, const void *target, const void *unit,
           const float feet[3], float stabilizer, float height, float heightChange) {
    if (Object::Guid(target) != Unit::Identity::PlayerGuid() || !IsActive(camera, unit))
        return false;
    void *model = Game::Read<void *>(unit, Offsets::OFF_CGOBJECT_MODEL);
    // Both engine calls wait for the model file while it has not loaded.
    if (model == nullptr || Game::Read<uint32_t>(model, Offsets::OFF_M2MODEL_LOADED) == 0)
        return false;
    const bool has55 = HasAttachment(model, kHeadAttachment);
    const bool has17 = HasAttachment(model, kPivotAttachment);
    if (!has55 && !has17)
        return false;
    s.rope[2] -= heightChange;
    float head[3];
    if (has55 && has17) {
        float a55[3], a17[3];
        AttachmentPosition(model, kHeadAttachment, a55);
        AttachmentPosition(model, kPivotAttachment, a17);
        for (int i = 0; i < 3; ++i)
            head[i] = (a55[i] - a17[i]) * 0.5f + a17[i];
    } else {
        AttachmentPosition(model, has55 ? kHeadAttachment : kPivotAttachment, head);
    }
    const float deadZone = Settings::HeadMovementDeadZone();
    const float delta[3] = {head[0] - feet[0], head[1] - feet[1],
                            head[2] - feet[2] - (stabilizer + height)};
    for (int i = 0; i < 3; ++i) {
        const float d = delta[i] - s.rope[i];
        const float size = std::fabs(d);
        if (deadZone < size)
            s.rope[i] += (size - deadZone) * (d > 0.0f ? 1.0f : (d < 0.0f ? -1.0f : 0.0f));
    }
    return true;
}

} // namespace

bool Apply(void *camera, const void *target, const float feet[3], float stabilizer, float dt,
           float offset[3], bool &tracked) {
    State &s = For(camera);
    tracked = false;
    const float height = Game::Read<float>(camera, Offsets::OFF_CAMERA_HEIGHT);
    const float heightChange = s.hasHeight ? height - s.height : 0.0f;
    s.hasHeight = true;
    s.height = height;
    if (target == nullptr)
        return false;
    const void *unit = View::Unit(target);
    tracked = Track(s, camera, target, unit, feet, stabilizer, height, heightChange);
    if (unit == nullptr)
        return false;
    const float k = IsActive(camera, unit) ? Strength(camera, unit) : 0.0f;
    float rate;
    if (std::fabs(View::TargetDistance(camera)) < View::kEpsilon)
        rate = Settings::HeadMovementFirstPersonDampRate();
    else if (Moving(camera))
        rate = Settings::HeadMovementMovingDampRate();
    else
        rate = Settings::HeadMovementStandingDampRate();
    const float e = std::exp(-(dt * rate));
    bool inPlay = false;
    for (int i = 0; i < 3; ++i) {
        const float goal = k * s.rope[i];
        s.offset[i] = goal - (goal - s.offset[i]) * e;
        offset[i] += s.offset[i];
        inPlay = inPlay || std::fabs(s.offset[i]) >= View::kEpsilon;
    }
    return inPlay;
}

} // namespace Camera::HeadMovement
