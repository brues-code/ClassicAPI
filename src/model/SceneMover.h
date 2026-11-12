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

// The client's mover, transcribed for a scene actor: CMovement's fields and
// the functions that move a unit on the ground, through the air and in the
// water, from the movement commands down to the contact sweep
// (SceneContact.h). Each function is one client function, in its order and
// precision. A Body is the scene's own; the client's movers, their manager
// and its lists are never touched.
//
// A body is the client's active mover (the unit the player controls), so a
// spline it finishes is reported and freed (CompleteSpline 0x00619DE0).
//
// Left out, as the scene has none of them: transports, flying splines
// (spline flag 0x200), the event queue and every packet, hover (0x40000000),
// water walking, fixed height (0x800) and the swim pitch setter
// (0x007C6F70), which the client drives from the camera.

#include "model/SceneContact.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Model::Scene::Mover {

// The spline record (0x00630C40, 0x70 bytes at CMovement +0xA4), the
// fields a ground move uses, and its Catmull-Rom curve (+0x2C, vtable
// 0x007FFD84) in mode 0: straight chords between the control points.
struct Spline {
    float facingAngle;              // +0x14, the facing at the end (flag 0x40000)
    uint32_t flags;                 // +0x18
    uint32_t start;                 // +0x1C, the tick it started at
    int32_t elapsed;                // +0x20, ms
    uint32_t duration;              // +0x24, ms
    float length;                   // +0x30, the curve's total length (curve +0x04)
    std::vector<std::array<float, 3>> points; // +0x3C, the curve's control points (curve +0x10)
    std::vector<float> segments;    // +0x4C, the curve's segment lengths (curve +0x20)
    float end[3];                   // +0x58, the last point
};

// CMovement, the fields a scene body uses (offsets from 0x00616FA0).
struct Body {
    float position[3];     // +0x10
    float facing;          // +0x1C
    float pitch;           // +0x20
    uint32_t flags;        // +0x40
    float anchor[3];       // +0x44, the position at the anchors
    float anchorFacing;    // +0x50
    float anchorPitch;     // +0x54
    uint32_t elapsed;      // +0x58, ms since the anchors
    float direction[3];    // +0x5C
    float heading[2];      // +0x68
    float pitchBasis[2];   // +0x70, cos and sin of the anchor pitch
    uint32_t fallTime;     // +0x78, ms into the fall
    float fallStartZ;      // +0x7C
    float stepFloor;       // +0x80, the z a step began at
    float speed;           // +0x84
    float walkSpeed;       // +0x88
    float runSpeed;        // +0x8C
    float runBackSpeed;    // +0x90
    float swimSpeed;       // +0x94
    float swimBackSpeed;   // +0x98
    float turnRate;        // +0x9C
    float fallSpeed;       // +0xA0, down positive
    std::optional<Spline> spline; // +0xA4, null when absent
    float radius;          // +0xB0
    float height;          // +0xB4
    float splashDepth;     // the unit's +0x9A4: the depth the last splash test saw
    float stepRatio;       // +0xB8
    uint32_t clock;        // the movement manager's +0x128: the tick moved to
    Contact::Facets facets; // the world query's list (0x00C4E52C)
    // The unit's side of each substep (BroadcastStateChange 0x0061A820,
    // from the substep loop at 0x00634260): `oldFlags` the flags before the
    // substep, `jumped` whether its fall had a start speed, `arrived` whether
    // the body reached its spline's end and stopped (MSG_MOVE_STOP, after the
    // spline's done report). Null for none.
    void (*stepped)(void *context, uint32_t oldFlags, bool jumped, bool arrived) = nullptr;
    void *steppedContext = nullptr;
};

// The client's flag bits (+0x40) a scene body takes.
enum : uint32_t {
    FLAG_FORWARD = 0x1,
    FLAG_BACKWARD = 0x2,
    FLAG_STRAFE_LEFT = 0x4,
    FLAG_STRAFE_RIGHT = 0x8,
    FLAG_TURN_LEFT = 0x10,
    FLAG_TURN_RIGHT = 0x20,
    FLAG_WALK = 0x100,
    FLAG_PITCH_UP = 0x40,
    FLAG_PITCH_DOWN = 0x80,
    FLAG_FALLING = 0x2000,
    FLAG_FALLING_FAR = 0x4000,
    // The deferred stop: a landing stops the move (ApplyLatches 0x0061AB20).
    FLAG_STOP_ON_LAND = 0x8000,
    FLAG_SWIMMING = 0x200000,
    FLAG_ON_STEP = 0x4000000,
};

// The constructor (0x00616FA0, 0x007C4850) at `p` facing `facing`, at the
// tick `now`, with the speeds a server's create block gives a character.
void Init(Body &body, const float (&p)[3], float facing, uint32_t now);
// CMovement::SetCollisionSize (0x006174B0): a model's collision width and
// height at `scale`, `displayScale` the display's CreatureModelScale.
void SetCollisionSize(Body &body, float width, float height, float scale, float displayScale);
// The movement manager's tick (0x00616800, 0x00616620) up to `now`. True
// when time passed, where the client then runs the unit's step
// (CGUnit_C::OnMoverStepped 0x005FAC70): the swim decision below.
bool Tick(Body &body, uint32_t now);
// The position reset (0x007C69A0): put down at `p` facing `facing`, at rest.
void Reset(Body &body, const float (&p)[3], float facing);
// SetFacing (0x007C6F30).
void SetFacing(Body &body, float facing);
// The fall gate (0x007C61C0): starts a fall from rest unless already in
// the air, swimming or rooted.
bool FallGate(Body &body);

// The movement commands. Each takes effect at the body's tick: Tick first.
bool StartMove(Body &body, bool forward);    // 0x007C6AE0
bool StopMove(Body &body);                   // 0x007C6BA0
bool StartStrafe(Body &body, bool left);     // 0x007C6C50
bool StopStrafe(Body &body);                 // 0x007C6D30
void StartTurn(Body &body, bool left);       // 0x007C6D90
bool StopTurn(Body &body);                   // 0x007C6DD0
void StartPitch(Body &body, bool up);        // 0x007C6DF0, only while swimming
bool StopPitch(Body &body);                  // 0x007C6E30
bool Jump(Body &body);                       // 0x007C6230 with check 1
void SetRunMode(Body &body, bool run);       // 0x007C71C0

// A server's knockback, as the client launches it (0x006179C0, from the
// drain's knockback entry and the relayed MSG_MOVE_KNOCK_BACK 0x00617A60):
// swimming ends (0x007C6E80); a fall starts at `speedZ` (0x007C61F0, down
// positive, so a launch upward is negative); the direction (cosine, sine)
// is world-absolute and becomes the heading and the direction; backward
// clears and forward sets with the stop-on-landing latch, whatever the
// speeds; `speedXY` is the speed. Nothing refuses it.
void KnockBack(Body &body, float cosine, float sine, float speedXY, float speedZ);

// The speeds a server changes (the spline speed opcodes 0x2FE to 0x303).
enum class Speed { Walk, Run, RunBack, Swim, SwimBack, Turn };
// CMovement's setters: SetWalkSpeed 0x007C70D0, SetRunSpeed 0x007C7030,
// SetRunBackSpeed 0x007C7080, SetSwimSpeed 0x007C7120, SetSwimBackSpeed
// 0x007C7170 and SetTurnRate 0x007C6FF0. Each stores `value` unless it is
// within 2^-22 of the field (a NaN stores), then re-anchors and, except the
// turn rate, refreshes the current speed. True when it stored.
bool SetSpeed(Body &body, Speed which, float value);
float GetSpeed(const Body &body, Speed which);
// ClearSpline (0x007C6310): what the client does first with a move event
// that is not the player's own (UpdatePlayerMovement 0x00615C30).
void ClearSpline(Body &body);

// A server's move of the body to `to` along the ground, as the client reads
// and starts it (CGUnit_C::OnMonsterMove 0x006018F0, mover side): a path
// from where the body stands, at its own speed (swim, walk or run), ending
// at `facing` when given. True when the body set off; false when it stopped
// where it stands, which the client reports as the spline done at once.
bool MoveTo(Body &body, const float (&to)[3], const float *facing);
// Whether the body follows a spline (+0xA4 set, its flag 0x4 clear).
bool OnSpline(const Body &body);

// The unit's swim decision (CGUnit_C::UpdateSwimState 0x006030C0) for a
// player-controlled unit, after a tick: `depth` is the liquid's surface less
// the feet's z, 0 out of liquid. A body starts swimming deeper than three
// quarters of its height, unless it is still rising from a jump, and stops
// out of the liquid or 1/36 yard shallower than that. True when the unit
// splashes: not swimming before, the depth crossed two fifths of its height
// since the last test, either way.
bool UpdateSwimState(Body &body, bool inLiquid, float depth);

// GetCurrentFallSpeed (0x007C5D70): the vertical speed now, down positive.
float FallSpeed(const Body &body);
// GetCurrentSpeed (0x007C4C90, walk 0): the speed the flags pick, 0 with no
// direction.
float CurrentSpeed(const Body &body);

// The pitch the client poses a unit's model with (the unit's pose 0x00608560,
// 0x0060A110): a swimmer moving forward or back is turned by 2π less its
// pitch about its own y axis (the euler builder 0x007BF4B0; nose up for a
// rising swimmer), any other body not at all.
float ModelPitch(const Body &body);

} // namespace Model::Scene::Mover
