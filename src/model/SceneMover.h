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
// the functions that move a unit on the ground and through the air, from the
// movement commands down to the contact sweep (SceneContact.h). Each function
// is one client function, in its order and precision. A Body is the scene's
// own; the client's movers, their manager and its lists are never touched.
//
// Left out, as the scene has none of them: transports, splines, the event
// queue and every packet, hover (0x40000000), water walking and the
// pitch bits. Swimming (the free-move resolver 0x00634640) is not ported yet,
// and no command here sets 0x200000.

#include "model/SceneContact.h"

#include <cstdint>

namespace Model::Scene::Mover {

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
    float radius;          // +0xB0
    float height;          // +0xB4
    float stepRatio;       // +0xB8
    uint32_t clock;        // the movement manager's +0x128: the tick moved to
    Contact::Facets facets; // the world query's list (0x00C4E52C)
};

// The client's flag bits (+0x40) a scene body takes.
enum : uint32_t {
    FLAG_FORWARD = 0x1,
    FLAG_BACKWARD = 0x2,
    FLAG_STRAFE_LEFT = 0x4,
    FLAG_STRAFE_RIGHT = 0x8,
    FLAG_TURN_LEFT = 0x10,
    FLAG_TURN_RIGHT = 0x20,
    FLAG_FALLING = 0x2000,
    FLAG_FALLING_FAR = 0x4000,
    FLAG_SWIMMING = 0x200000,
    FLAG_ON_STEP = 0x4000000,
};

// The constructor (0x00616FA0, 0x007C4850) at `p` facing `facing`, at the
// tick `now`, with the speeds a server's create block gives a character.
void Init(Body &body, const float (&p)[3], float facing, uint32_t now);
// CMovement::SetCollisionSize (0x006174B0): a model's collision width and
// height at `scale`, `displayScale` the display's CreatureModelScale.
void SetCollisionSize(Body &body, float width, float height, float scale, float displayScale);
// The movement manager's tick (0x00616800, 0x00616620) up to `now`.
void Tick(Body &body, uint32_t now);
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
bool Jump(Body &body);                       // 0x007C6230 with check 1

// GetCurrentFallSpeed (0x007C5D70): the vertical speed now, down positive.
float FallSpeed(const Body &body);

} // namespace Model::Scene::Mover
