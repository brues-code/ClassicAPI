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

// Target focus, ported from the 3.80.2 Classic client (WowClassicGM-China.exe
// 3.80.2.69496, CGCamera_ApplyTargetFocus 0x141bc83e0; docs/notes/
// actioncam-3802.md A §3.6). It runs after the pitch delta and turns the
// view only.
//
//   - The focus. While the interaction object is set (with
//     test_cameraTargetFocusInteractEnable), it is that object. Otherwise
//     (with test_cameraTargetFocusEnemyEnable and the character not kept
//     centered) it is the current target, when it is not the player and
//     the player can attack it (FUN_1414ac980, the attack test 3.80.2's
//     soft-target enemy check also calls).
//   - The aim point is 0.6 of the focus's height above its position,
//     taken from the pivot and cut to max(1.5 x live distance, 5) yards.
//   - The weights: full up to 50 yards from the character, nothing past 80;
//     full within 60 degrees of yaw, nothing past 180; less as the view
//     looks down past 30 degrees, nothing past 88.
//   - The turn: the interact or enemy strengths times the weights. Yaw
//     past 20 degrees counts 0.3; the result is held within 0.15 (pitch)
//     and 0.25 (yaw) of the field of view.
//   - The ease: a 2-D critically damped spring, stiffness 20 while focused
//     and 15 when released, from when the focus GUID changes until it is
//     within 0.1 degree; otherwise the turn is taken as it is.
//   - The basis is rebuilt from the view's yaw and pitch plus the turn.
//
// 1.12 pieces:
//   - The interaction object is 1.12's interaction target
//     (VAR_INTERACTION_TARGET_GUID 0x00B4E2D0), which only the
//     interaction begin and the closer write. It holds the unit or object
//     whose window is open; 1.12 keeps loot out of it.
//   - 3.80.2 also drops the interaction object when byte +0x18 mask 0x01 of
//     its object (entity+0x30) is clear. It runs the same test on every
//     GUID-to-camera-object lookup (SCENE_SetGameCameraTargetActor,
//     CGCamera_UpdatePerFrame, ...), so it is a "usable as a scene object"
//     gate. Its setter was not found. 1.12 has no counterpart: an object
//     the manager resolves always has a position, and 3.3.5's
//     ClntObjMgrObjectPtr checks only presence and type. So the lookup
//     here is the same.
//   - The attack test is 1.12's CanAttack (FUN_UNIT_CAN_ATTACK).
//   - The height is the model box's (bounds +0xC0 × scale +0x90), the
//     height the soft-targeting port uses for 3.80.2's object height.
//   - The angles are measured on the world rows (the basis is
//     transport-local), and the turn is added to the local yaw and pitch,
//     which a yaw-only transport rotation leaves the same. SetFacing
//     0x0050AB50 rebuilds the basis with the live roll (+0xF8).
//   - The view-mode test is far sight, as for the other features.

#include "camera/TargetFocus.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/Spring.h"
#include "camera/View.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/Relation.h"

#include <cmath>
#include <cstdint>

namespace Camera::TargetFocus {

namespace {

constexpr uint32_t kHolding = 0x40000000; // 3.80.2's +0xa8 bits
constexpr uint32_t kEasing = 0x80000000;
constexpr float kAimHeight = 0.6f;
constexpr float kNearDistance = 50.0f;
constexpr float kFarScale = 0.033333335f;       // 1/30 yards
constexpr float kYawFree = 1.0471976f;          // 60 degrees
constexpr float kYawScale = 0.47746482f;        // 1/120 degrees
constexpr float kPitchFree = 0.5235988f;        // 30 degrees
constexpr float kPitchScale = 0.9878583f;       // 1/58 degrees
constexpr float kReachScale = 1.5f;
constexpr float kMinReach = 5.0f;
constexpr float kMinAimSq = 0.1f;
constexpr float kSoftYaw = 0.34906584f;         // 20 degrees
constexpr float kSoftYawShare = 0.3f;
constexpr float kPitchFov = 0.15f;
constexpr float kYawFov = 0.25f;
constexpr float kFocusOmega = 20.0f;            // 0x41a00000
constexpr float kReleaseOmega = 15.0f;          // 0x41700000
constexpr float kSettle = 0.0017453292f;        // 0.1 degree
constexpr float kPi = 3.14159265f;

struct State {
    const void *camera = nullptr;
    uint32_t flags = 0;
    uint64_t focusGuid = 0;            // 3.80.2's +0x408
    Spring pitch = {0.0f, 0.0f};       // +0x3f8 and its velocity
    Spring yaw = {0.0f, 0.0f};         // +0x3fc
};

State g_state;

State &For(const void *camera) {
    if (g_state.camera != camera) {
        g_state = State{};
        g_state.camera = camera;
    }
    return g_state;
}

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v < 1.0f ? v : 1.0f); }

float Sign(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

// FUN_142a5c410: an angle wrapped into -π..π.
float Wrap(float a) {
    while (a > kPi)
        a -= 2.0f * kPi;
    while (a < -kPi)
        a += 2.0f * kPi;
    return a;
}

float Yaw(const float v[3]) { return std::atan2(v[1], v[0]); }

bool ModeAllows(const void *camera) {
    return !View::FarSight(camera) && std::fabs(View::TargetDistance(camera)) >= View::kEpsilon;
}

// The focus object, or null; `guid` is the GUID 3.80.2 tracks for its
// change test (the candidate's, even when it fails), `interact` which
// strengths apply.
void *Focus(const void *camera, uint64_t &guid, bool &interact) {
    guid = 0;
    interact = false;
    const uint64_t playerGuid = Unit::Identity::PlayerGuid();
    void *player = playerGuid != 0 ? Object::ByGuid(Offsets::TYPEMASK_UNIT, playerGuid) : nullptr;
    if (player == nullptr)
        return nullptr;
    const uint64_t interaction = Game::Read<uint64_t>(Offsets::VAR_INTERACTION_TARGET_GUID);
    if (Settings::TargetFocusInteractEnable() && ModeAllows(camera) && interaction != 0) {
        guid = interaction;
        interact = true;
        return Object::ByGuid(Offsets::TYPEMASK_OBJECT, interaction);
    }
    if (Settings::KeepCharacterCentered() || !Settings::TargetFocusEnemyEnable() ||
        !ModeAllows(camera))
        return nullptr;
    const uint64_t target = Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO);
    if (target == 0 || target == playerGuid)
        return nullptr;
    guid = target;
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, target);
    return unit != nullptr && Unit::Relation::CanAttack(player, unit) ? unit : nullptr;
}

void Position(void *object, float out[3]) {
    using GetPosition_t = float *(__thiscall *)(void *, float *);
    auto *vtable = Game::Read<uintptr_t *>(object, 0);
    const float *p = reinterpret_cast<GetPosition_t>(
        vtable[Offsets::OFF_CGOBJECT_VTBL_GET_POSITION / sizeof(uintptr_t)])(object, out);
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
}

float Height(const void *object) {
    const float scale = Game::Read<float>(object, Offsets::OFF_CGOBJECT_SCALE);
    const float *b = Game::Ptr<float>(object, Offsets::OFF_CGOBJECT_BOUNDS);
    return (b[5] - b[2]) * scale;
}

float Fov(void *camera) {
    using Fov_t = float(__thiscall *)(void *);
    auto *vtable = Game::Read<uintptr_t *>(camera, 0);
    return reinterpret_cast<Fov_t>(vtable[Offsets::VTBL_CAMERA_FOV / sizeof(uintptr_t)])(camera);
}

} // namespace

void Apply(void *camera, const float feet[3], const float pivot[3], float dt) {
    State &s = For(camera);
    uint64_t guid;
    bool interact;
    void *focus = Focus(camera, guid, interact);
    if ((s.flags & kHolding) == 0 && focus == nullptr)
        return;

    float forward[3];
    View::Row(camera, Offsets::VTBL_CAMERA_FORWARD, forward);
    const float yaw0 = Yaw(forward);
    const float pitch0 = View::PitchAngle(forward[0], forward[1], forward[2]);
    float goalPitch = 0.0f;
    float goalYaw = 0.0f;
    if (focus != nullptr) {
        s.flags |= kHolding;
        float p[3];
        Position(focus, p);
        p[2] += Height(focus) * kAimHeight;
        float d[3] = {p[0] - pivot[0], p[1] - pivot[1], p[2] - pivot[2]};
        const float fx = p[0] - feet[0];
        const float fy = p[1] - feet[1];
        const float fz = p[2] - feet[2];
        const float range = std::sqrt(fx * fx + fy * fy + fz * fz);
        float wDistance = 1.0f;
        if (kNearDistance < range)
            wDistance = 1.0f - Clamp01((range - kNearDistance) * kFarScale);
        float wAngle =
            1.0f - Clamp01((std::fabs(Wrap(std::atan2(d[1], d[0]) - yaw0)) - kYawFree) * kYawScale);
        if (kPitchFree < pitch0)
            wAngle *= 1.0f - Clamp01((pitch0 - kPitchFree) * kPitchScale);
        float reach = Game::Read<float>(camera, Offsets::OFF_CAMERA_DISTANCE) * kReachScale;
        if (reach <= kMinReach)
            reach = kMinReach;
        const float lengthSq = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (reach * reach < lengthSq) {
            const float k = reach / std::sqrt(lengthSq);
            d[0] *= k;
            d[1] *= k;
            d[2] *= k;
        }
        const float *eye = Game::Ptr<float>(camera, Offsets::OFF_CAMERA_POSITION);
        float v[3] = {pivot[0] + d[0] - eye[0], pivot[1] + d[1] - eye[1], pivot[2] + d[2] - eye[2]};
        if (v[0] * v[0] + v[1] * v[1] + v[2] * v[2] < kMinAimSq)
            return;
        const float yawT = Yaw(v);
        const float pitchT = View::PitchAngle(v[0], v[1], v[2]);
        const float sp = Clamp01(interact ? Settings::TargetFocusInteractStrengthPitch()
                                          : Settings::TargetFocusEnemyStrengthPitch());
        const float sy = Clamp01(interact ? Settings::TargetFocusInteractStrengthYaw()
                                          : Settings::TargetFocusEnemyStrengthYaw());
        const float dp = Wrap(pitchT - pitch0) * sp * wAngle * wDistance;
        float dy = Wrap(yawT - yaw0) * sy * wAngle * wDistance;
        float yawSize = std::fabs(dy);
        const float pitchSign = Sign(dp);
        const float yawSign = Sign(dy);
        if (kSoftYaw < yawSize) {
            yawSize = (yawSize - kSoftYaw) * kSoftYawShare + kSoftYaw;
            dy = yawSign * yawSize;
        }
        float pitch = dp + pitch0;
        float yaw = dy + yaw0;
        const float fov = Fov(camera);
        if (fov * kPitchFov < std::fabs(dp))
            pitch -= (std::fabs(dp) - fov * kPitchFov) * pitchSign;
        if (fov * kYawFov < yawSize)
            yaw -= (yawSize - fov * kYawFov) * yawSign;
        goalPitch = pitch - pitch0;
        goalYaw = yaw - yaw0;
    }
    if (guid != s.focusGuid) {
        s.flags |= kEasing;
        s.focusGuid = guid;
    }
    if ((s.flags & kEasing) != 0) {
        SmoothCriticallyDamped2(s.pitch, s.yaw, goalPitch, goalYaw,
                                focus != nullptr ? kFocusOmega : kReleaseOmega, dt);
        if (std::fabs(goalPitch - s.pitch.value) < kSettle &&
            std::fabs(goalYaw - s.yaw.value) < kSettle)
            s.flags &= ~kEasing;
    } else {
        s.pitch = {goalPitch, 0.0f};
        s.yaw = {goalYaw, 0.0f};
    }
    if (View::kEpsilon <= s.yaw.value * s.yaw.value + s.pitch.value * s.pitch.value) {
        const float *basis = Game::Ptr<float>(camera, Offsets::OFF_CAMERA_BASIS);
        const float localYaw = Yaw(basis);
        const float localPitch = View::PitchAngle(basis[0], basis[1], basis[2]);
        using SetFacing_t = void(__thiscall *)(void *, float, float, float);
        reinterpret_cast<SetFacing_t>(Offsets::FUN_CAMERA_SET_FACING)(
            camera, localYaw + s.yaw.value, localPitch + s.pitch.value,
            Game::Read<float>(camera, Offsets::OFF_CAMERA_ROLL));
    } else if (focus == nullptr) {
        s.flags &= ~(kHolding | kEasing);
    }
}

} // namespace Camera::TargetFocus
