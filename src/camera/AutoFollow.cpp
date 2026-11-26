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
//   - "can turn the character" (FUN_1412610e0): 1.12's MayTurn
//     0x005145B0, the camera following the player, and the same input bits
//     (1.12's mouselook command bit stands for 3.80.2's mouselook mode and
//     mouse button; its 0x10000000 bit has no 1.12 counterpart).
//   - the combat test's player GUID +0x6a8: the melee victim, 1.12's
//     +0xC48 (inferred: 3.80.2's field was not traced to a writer).
//
// With CameraFollowTarget the camera instead turns toward the locked target
// (in combat, by default; not in PvP unless CameraFollowTargetPvp). While
// the camera yaw is relative to the character (no absolute frame, so not
// under mouselook or gamepad freelook) and the character can turn, part of
// the turn goes to the character, and past CameraFollowSnapCharacterAngle a
// moving character snaps to the camera.
//
// On a boat or zeppelin the step works in the transport's frame, as 3.80.2
// does (FUN_141b65290): the move is the target's displacement relative to
// the transport's position, and the yaw delta subtracts the transport's
// facing, so the transport carrying the character turns nothing. 1.12 keeps
// the same pair: the camera holds the transport GUID (+0x98) and the engine's
// ride frame is the transport's position and yaw (vtable +0x14 / +0x18,
// benilla collision/ride "Ride frame"). The eye (+0x08), the last pivot
// (+0x174) and the target position are world; the mover's facing, the
// relative yaw and the basis +0x14 are transport-local, so the facing half
// needs no conversion. One deviation: 3.80.2 records the transport only at
// the end of a camera update, so the frame that boards or leaves one
// compares against a record from another frame and reads the transport's
// whole world offset as movement. Here a changed transport counts last
// frame's as this frame's, which leaves that frame's plain world movement.

#include "camera/AutoFollow.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/View.h"
#include "gamepad/Movement.h"
#include "object/Resolve.h"
#include "pvp/Context.h"
#include "target/Reason.h"
#include "time/Clock.h"
#include "unit/Identity.h"
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
constexpr float kFacingEps = 9.5367432e-07f; // a facing turn smaller than this is dropped

struct State {
    void *camera = nullptr;
    bool hasRecord = false;
    float lastFeet[3] = {};     // last frame's target position
    float smoothed[3] = {};     // smoothed movement (rate 5/s)
    float ease = 1.0f;
    bool initialized = false;
    bool resuming = false;      // inside the adjust delay
    uint32_t seen = 0;          // last stamp this step saw
    float pending = 0.0f;       // yaw to give back after a character snap
    uint64_t lockGuid = 0;      // the locked target followed last
    uint64_t lastTransport = 0; // the record's transport GUID
    float lastTransportPos[3] = {};
    float lastTransportFacing = 0.0f;
};

// FUN_141b65290: the camera's transport as a world position and facing,
// zeros without one or when the object is not known.
struct Transport {
    uint64_t guid = 0;
    float pos[3] = {};
    float facing = 0.0f;
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

Transport TransportOf(const void *camera) {
    Transport t;
    t.guid = Game::Read<uint64_t>(camera, Offsets::OFF_CAMERA_TRANSPORT_GUID);
    if (t.guid == 0)
        return t;
    void *object = Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, t.guid);
    if (object == nullptr || !Unit::Position::Read(object, t.pos)) {
        t.pos[0] = t.pos[1] = t.pos[2] = 0.0f;
        return t;
    }
    t.facing = Unit::Position::Facing(object);
    return t;
}

float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void Reset(State &s) {
    s.smoothed[0] = s.smoothed[1] = s.smoothed[2] = 0.0f;
    s.ease = 1.0f;
    s.pending = 0.0f;
    s.initialized = false;
    s.resuming = false;
    s.lockGuid = 0;
}

uint32_t InputFlags() {
    const void *ic = Game::Read<void *>(Offsets::VAR_UI_INPUT_CONTROLLER);
    return ic != nullptr ? Game::Read<uint32_t>(ic, Offsets::OFF_UI_INPUT_FLAGS) : 0;
}

uint32_t UnitFlags(const void *unit) {
    const uint8_t *fields = Object::Fields(unit);
    return fields != nullptr ? Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) : 0;
}

uint32_t MoveFlags(const void *unit) {
    const uint8_t *mv = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    return mv != nullptr ? Game::Read<uint32_t>(mv, Offsets::OFF_MOVEMENT_FLAGS) : 0;
}

// 3.80.2's mouselook or freelook: its mouselook mode, right mouse button or
// freelook (input+0x20 == 1, flags 0x2000001). 1.12's mouselook mode and its
// button are one command bit (MouselookStart and IsMouselooking use it).
bool Looking(uint32_t flags) {
    return (flags & Offsets::INPUT_FLAG_MOUSELOOK) != 0 || GamePad::Movement::FreelookOn();
}

// §3.3: the locked target, or null.
void *LockedTarget(State &s, void *player) {
    bool follow = true;
    if (Settings::FollowTargetCombat() && player != nullptr) {
        const uint32_t flags = UnitFlags(player);
        follow = (flags & Offsets::UNIT_FLAG_PET_IN_COMBAT) != 0 ||
                 Game::Read<uint64_t>(player, Offsets::OFF_UNIT_ATTACK_TARGET) != 0 ||
                 (flags & Offsets::UNIT_FLAG_IN_COMBAT) != 0;
    }
    const uint64_t guid = Target::Reason::Selection();
    if (!follow || guid == 0 || Target::Reason::IsLoose())
        return nullptr;
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    if (unit == nullptr)
        return nullptr;
    if (!Settings::FollowTargetPvp() &&
        (Pvp::Context::PvpContext(player) || Pvp::Context::PvpUnit(player, unit)))
        return nullptr;
    if (s.lockGuid != guid)
        s.ease = 0.75f;
    s.lockGuid = guid;
    return unit;
}

// FUN_1412610e0(input, 0): the character may turn and is being driven.
bool CanTurnCharacter(const void *camera, void *player) {
    if (player == nullptr || reinterpret_cast<int(__stdcall *)(void *)>(Offsets::FUN_MAY_TURN)(player) == 0)
        return false;
    if (Game::Read<uint64_t>(camera, Offsets::OFF_CAMERA_TARGET_GUID) != Unit::Identity::PlayerGuid())
        return false;
    const uint32_t flags = InputFlags();
    if (Looking(flags))
        return true;
    // FUN_14125c210: strafing (the turn keys count only while looking).
    if ((flags & (Offsets::INPUT_FLAG_STRAFE_LEFT | Offsets::INPUT_FLAG_STRAFE_RIGHT)) != 0)
        return true;
    return (flags & (Offsets::INPUT_FLAG_MOVE_FORWARD | Offsets::INPUT_FLAG_MOVE_BACKWARD |
                     Offsets::INPUT_FLAG_AUTORUN)) != 0;
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
bool Gated(const void *camera, const State &s, bool onStick, bool follow) {
    if (!onStick && !follow)
        return true;
    const uint32_t mode = Game::Read<uint32_t>(camera, Offsets::OFF_CAMERA_MODE);
    if ((mode & Offsets::CAMERA_MODE_MOUSELOOK) != 0 && !GamePad::Movement::FreelookOn())
        return true;
    if ((mode & (Offsets::CAMERA_MODE_YAW_TRANSITION | Offsets::CAMERA_MODE_PITCH_TRANSITION |
                 Offsets::CAMERA_MODE_FAR_SIGHT)) != 0)
        return true;
    if (std::fabs(View::TargetDistance(camera)) < kEps)
        return true;
    return !s.hasRecord;
}

// What FollowYaw needs about this frame beyond the move.
struct Frame {
    void *lock;        // the locked target, or null
    void *player;      // the active player, or null
    const void *unit;  // the camera target as a unit, or null
    bool pendingRan;   // §3.2 gave back a snap this frame
    float facing;      // the transport's facing now, 0 without one
    float lastFacing;  // and at the record
    uint32_t now;
    float dt;
};

// §3.6.
void FollowYaw(void *camera, State &s, const Frame &f, const float move[3], const float toPivot[3]) {
    const float yawSpeed = Settings::FollowYawSpeed();
    if (yawSpeed <= 0.0f || move[0] * move[0] + move[1] * move[1] <= kMoveEpsSq)
        return;
    float k = 2.0f;
    if (f.lock == nullptr) {
        float forward[3];
        View::Row(camera, Offsets::VTBL_CAMERA_FORWARD, forward);
        const float length = std::sqrt(Dot3(move, move));
        const float dir[3] = {move[0] / length, move[1] / length, move[2] / length};
        const float along = std::fabs(Dot3(forward, dir));
        k = 1.5f * (1.0f - along) + 4.0f * along;
    }
    k = Clamp(k * yawSpeed, 0.0f, 100.0f);
    const float p[3] = {toPivot[0] + move[0] * k, toPivot[1] + move[1] * k, toPivot[2] + move[2] * k};
    const float steep = std::fmin(std::fabs(View::PitchAngle(p[0], p[1], p[2])) * kInvPitchLimit, 1.0f);
    float delta = WrapPi((Norm2Pi(std::atan2(p[1], p[0])) - f.facing) -
                         (Norm2Pi(std::atan2(toPivot[1], toPivot[0])) - f.lastFacing));
    delta *= (1.0f - steep) * s.ease;
    if (s.resuming && std::fabs(s.ease - 1.0f) < kEps && std::fabs(delta) < kEps)
        s.resuming = false;
    if (std::fabs(s.ease) < kEps)
        return;
    // The facing half: only while the camera yaw is relative to the
    // character, which then is the camera's target.
    if (Game::Read<int>(camera, Offsets::OFF_CAMERA_ABSOLUTE_FRAMES) < 1 && CanTurnCharacter(camera, f.player)) {
        const float relYaw = Game::Read<float>(camera, Offsets::OFF_CAMERA_RELATIVE_YAW);
        const float facing = Unit::Position::MoverFacing(f.player);
        const float camYaw = Norm2Pi(relYaw + Game::Read<float>(camera, Offsets::OFF_CAMERA_YAW_FLIP) + facing);
        float diff = WrapPi(camYaw - facing);
        const float snap = Settings::FollowSnapCharacterAngle() * kDegToRad;
        const uint32_t mf = f.unit != nullptr ? MoveFlags(f.unit) : 0;
        const bool forwardBack = (mf & (Offsets::MOVEFLAG_FORWARD | Offsets::MOVEFLAG_BACKWARD)) != 0;
        const bool strafing = (mf & (Offsets::MOVEFLAG_STRAFE_LEFT | Offsets::MOVEFLAG_STRAFE_RIGHT)) != 0;
        if (f.pendingRan || s.resuming || snap <= 0.0f || std::fabs(diff) <= snap || f.unit == nullptr ||
            (!strafing && !forwardBack)) {
            if (delta * diff >= 0.0f) {
                // Near the facing: the relative yaw eases back behind.
                if (std::fabs(diff) < 15.0f * kDegToRad ||
                    (std::fabs(diff) < 45.0f * kDegToRad && f.unit != nullptr && forwardBack)) {
                    const float dist = Clamp(View::TargetDistance(camera), 0.0f, 50.0f);
                    const float r = std::fmax((1.0f - dist * 0.015f) * yawSpeed, 0.0f);
                    const float y = WrapPi(relYaw);
                    ShiftYaw(camera, WrapPi(Approach(y, 0.0f, r, f.dt) - y));
                }
            } else {
                // Turning back toward the facing: the camera takes it first.
                const float t = std::fabs(diff) <= std::fabs(delta) ? -diff : delta;
                ShiftYaw(camera, t);
                delta -= t;
                diff += t;
            }
            // The rest turns the character, when no turn key fights it.
            const uint32_t flags = InputFlags();
            if (std::fabs(delta) >= kFacingEps && std::fabs(diff) < 45.0f * kDegToRad &&
                ((flags & (Offsets::INPUT_FLAG_TURN_LEFT | Offsets::INPUT_FLAG_TURN_RIGHT)) == 0 ||
                 Looking(flags))) {
                Unit::Position::SetFacing(f.player, f.now, facing + delta);
                delta = 0.0f;
            }
        } else {
            // Moving and too far off: the character snaps to the camera, and
            // next frame's §3.2 turns the camera back so the view holds.
            s.pending = -delta - diff;
            Unit::Position::SetFacing(f.player, f.now, camYaw + delta);
        }
    }
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
    const bool onStick = Settings::FollowOnStick();
    const bool follow = Settings::FollowTarget();
    if (target == nullptr || Gated(camera, s, onStick, follow)) {
        Reset(s);
        return;
    }

    // §3.2: give back last frame's snap.
    const bool pendingRan = std::fabs(s.pending) >= kEps;
    if (pendingRan) {
        ShiftYaw(camera, s.pending);
        s.pending = 0.0f;
    }

    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    void *lock = follow ? LockedTarget(s, player) : nullptr;

    float position[3];
    if (!Unit::Position::Read(target, position))
        return;
    // The transport now and at the record; a changed one stands in for the
    // record's (see the header).
    const Transport transport = TransportOf(camera);
    const bool sameTransport = s.lastTransport == transport.guid;
    const float *lastTransportPos = sameTransport ? s.lastTransportPos : transport.pos;
    const float lastTransportFacing = sameTransport ? s.lastTransportFacing : transport.facing;
    // §3.4. The last pivot is the driver's +0x174; its offset from the
    // feet stands for 3.80.2's pivot offset.
    const float *pivot = &Game::Ref<float>(camera, Offsets::OFF_CAMERA_LAST_PIVOT);
    const float *last = s.lastFeet;
    float move[3] = {0.0f, 0.0f, 0.0f};
    if (lock == nullptr) {
        s.lockGuid = 0;
        if (onStick) {
            // This frame's move in the transport's frame, scaled down closer
            // than 4 yards.
            const float w = std::fmin(View::TargetDistance(camera) * 0.25f, 1.0f);
            for (int i = 0; i < 3; ++i)
                move[i] = ((position[i] - transport.pos[i]) - (last[i] - lastTransportPos[i])) * w;
        }
    } else {
        // Toward the locked target at pivot height: a unit direction from 2
        // yards out, shorter inside.
        float lockPos[3];
        if (Unit::Position::Read(lock, lockPos)) {
            float v[3];
            for (int i = 0; i < 3; ++i)
                v[i] = lockPos[i] + (pivot[i] - last[i]) - position[i];
            const float d = std::sqrt(Dot3(v, v));
            if (d > kEps) {
                const float scale = d >= 2.0f ? 1.0f / d : 0.5f;
                for (int i = 0; i < 3; ++i)
                    move[i] = v[i] * scale;
            }
        }
    }

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
    const float *eye = &Game::Ref<float>(camera, Offsets::OFF_CAMERA_POSITION);
    const float toPivot[3] = {pivot[0] - eye[0], pivot[1] - eye[1], pivot[2] - eye[2]};
    const Frame f{lock, player, View::Unit(target), pendingRan, transport.facing, lastTransportFacing, now, dt};
    FollowYaw(camera, s, f, move, toPivot);
    FollowPitch(camera, target, s, toPivot, dt);
}

void Record(void *camera, const float feet[3]) {
    State &s = For(camera);
    s.lastFeet[0] = feet[0];
    s.lastFeet[1] = feet[1];
    s.lastFeet[2] = feet[2];
    const Transport transport = TransportOf(camera);
    s.lastTransport = transport.guid;
    s.lastTransportPos[0] = transport.pos[0];
    s.lastTransportPos[1] = transport.pos[1];
    s.lastTransportPos[2] = transport.pos[2];
    s.lastTransportFacing = transport.facing;
    s.hasRecord = true;
}

} // namespace Camera::AutoFollow
