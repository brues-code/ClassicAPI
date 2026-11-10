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

// An actor with the mover on (SetMovementEnabled) animates as the client's
// unit does from its own mover. The unit's chain lives on CGUnit_C, which an
// actor is not, so this is that chain on the actor, step for step:
//
//   1. The pick (SelectBaseAnimation 0x005FD8B0 with mask -1): the resolvers
//      a scene body can claim, in their order, and the airborne hold
//      between the first two and the rest.
//   2. The play (CGUnit_C::PlayAnimation 0x005FE2F0, PlayTrack 0x005FDBA0):
//      a move animation's rate and phase; nothing when the body already
//      plays the animation.
//   3. When it picks again: a movement command the mover took
//      (ProcessLocalMoveEvent 0x0060E0A0 -> AnimateMoveOpcode 0x0060E480);
//      a substep that arrives at a spline's end, lands, starts a fall or
//      ends a swim (BroadcastStateChange 0x0061A820 -> OnLand 0x00602D00,
//      UpdateLandingAnimation 0x00602C60, PlayFallAnimation 0x00602C40); a
//      body that catches up turning on the spot (the tail of CGUnit_C::Update
//      0x00607ED0); and a sequence that ends (OnAnimationDone 0x005FC920 ->
//      PlayFollowOnAnimation 0x005FC3F0).
//
// What the unit has and an actor does not: the death, special stand, loot,
// cast, ready, ranged, stand state and emote resolvers (an actor has no unit
// state; its script plays those), stealth and flying. An actor whose own
// animation is a death animation is dead, as Actor::PlayAnimation treats
// it: the chain leaves it to its script. A spell kit's animation (Casts) stays on top: a pick sets
// the actor's own animation under it, which the kit gives back when it ends.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "model/M2.h"
#include "model/SceneMover.h"

#include <cmath>
#include <cstdint>

namespace Model::Scene::Locomotion {

namespace {

constexpr uint32_t kNone = 208;               // AnimationData's row count: no animation
const Game::Live<float> kSprintSpeed{Offsets::VAR_MOVER_SPRINT_SPEED};
const Game::Live<float> kTurnEpsilon{Offsets::VAR_MOVER_FACING_EPSILON};
const Game::Live<float> kTurnEpsilonNegative{Offsets::VAR_MOVER_MINUS_FACING_EPSILON};

// The chain runs for an actor with the mover and a loaded model that is not
// dead.
bool Active(const Actor &actor) {
    return actor.mover != nullptr && actor.model != nullptr && actor.loaded &&
           !M2::IsDeathAnimation(static_cast<int>(actor.animation));
}

// A fall the pick holds through (0x005FD8EE to 0x005FD919): falling far, or
// a fall with a start speed that is not exactly 0 or NaN.
bool Airborne(const Mover::Body &b) {
    if ((b.flags & Mover::FLAG_FALLING) && (b.flags & Mover::FLAG_FALLING_FAR))
        return true;
    return (b.flags & Mover::FLAG_FALLING) && (b.fallSpeed < 0.0f || b.fallSpeed > 0.0f);
}

// ResolveMoveAnimation (0x005FD100) for a body with a direction bit.
uint32_t MoveAnimation(const Mover::Body &b) {
    const uint32_t f = b.flags;
    if (f & Mover::FLAG_SWIMMING) {
        if ((f & 0x3C) == 0)
            return (f & Mover::FLAG_BACKWARD) ? Offsets::ANIM_SWIM_BACKWARDS : Offsets::ANIM_SWIM;
        if (f & 0x30)
            return Offsets::ANIM_SWIM_IDLE;
        return (f & Mover::FLAG_STRAFE_LEFT) ? Offsets::ANIM_SWIM_LEFT : Offsets::ANIM_SWIM_RIGHT;
    }
    if (f & Mover::FLAG_BACKWARD)
        return Offsets::ANIM_WALK_BACKWARDS;
    const float speed = Mover::CurrentSpeed(b);
    if (kSprintSpeed <= speed)
        return Offsets::ANIM_SPRINT;
    if (b.walkSpeed + b.walkSpeed < speed)
        return Offsets::ANIM_RUN;
    return Offsets::ANIM_WALK;
}

// CanShuffle (0x005FCE30): turning, or the body catching up, and not in a
// swim or an emote, cast or ranged animation, or JumpEnd.
bool CanShuffle(const Actor &actor, bool jumpEndLock) {
    const Mover::Body &b = *actor.mover;
    if (!(b.flags & 0x30) && actor.facings.shuffle == 0)
        return false;
    if (b.flags & 0x40200800)
        return false;
    const int anim = actor.CurrentAnimation();
    if (M2::IsEmoteAnimation(anim) || M2::IsSpellCastAnimation(anim) || M2::IsThrownAnimation(anim) ||
        M2::IsBowAnimation(anim) || M2::IsRifleAnimation(anim))
        return false;
    return !jumpEndLock;
}

// SelectBaseAnimation (0x005FD8B0), mask -1. JumpEnd's lock (+0xD58 bit
// 0x4) holds while it plays, and its follow-on clears it before it picks.
uint32_t Select(const Actor &actor, bool jumpEndLock) {
    const Mover::Body &b = *actor.mover;
    if (Airborne(b))
        return kNone;
    if (b.flags & 0xF)
        return MoveAnimation(b);
    if (CanShuffle(actor, jumpEndLock))
        return (!(b.flags & Mover::FLAG_TURN_LEFT) && actor.facings.shuffle <= 0) ? Offsets::ANIM_SHUFFLE_RIGHT
                                                                                 : Offsets::ANIM_SHUFFLE_LEFT;
    if (jumpEndLock)
        return kNone; // ResolveIdleAnimation (0x005FD830) writes nothing
    return (b.flags & Mover::FLAG_SWIMMING) ? Offsets::ANIM_SWIM_IDLE : Offsets::ANIM_STAND;
}

bool JumpEndLock(const Actor &actor) { return actor.animation == Offsets::ANIM_JUMP_END; }

// CGUnit_C::PlayAnimation (0x005FE2F0) of `anim` on the body's track, as a
// body with no mount, swing, weapon or effect plays it: a move animation at
// the mover's speed over the sequence's (0x005FE50A to 0x005FE54D), starting
// at the phase of the move animation it replaces, and nothing when the track
// already plays it (PlayTrack 0x005FDBA0), unless that sequence just ended.
void Play(Actor &actor, uint32_t anim, bool ended) {
    if (!ended && actor.animation == anim)
        return;
    float rate = 1.0f;
    uint32_t offsetMs = 0;
    const M2::SequenceInfo info = M2::GetSequenceInfo(actor.model, anim, 0);
    if (0.0f < info.speed && M2::IsMoveAnimation(static_cast<int>(anim))) {
        rate = Mover::CurrentSpeed(*actor.mover) / info.speed;
        if (info.length != 0) {
            const M2::AnimState state = M2::GetAnimState(actor.model, -1);
            const M2::SequenceInfo was = M2::GetSequenceInfo(actor.model, state.id, state.variation);
            // 32-bit, as the client's imul and two divs (0x005FE606): the
            // product wraps.
            if (0.0f < was.speed && was.length != 0)
                offsetMs = state.position * info.length / was.length % info.length;
        }
    }
    actor.animation = anim;
    actor.variation = -1;
    actor.animSpeed = rate;
    actor.animOffsetSec = static_cast<float>(offsetMs) / 1000.0f;
    if (actor.kitAnimation < 0)
        actor.PlayAnimation();
}

// UpdateBaseAnimation (0x005FD9E0), mask -1.
void Pick(Actor &actor, bool jumpEndLock, bool ended) {
    const uint32_t anim = Select(actor, jumpEndLock);
    if (anim != kNone)
        Play(actor, anim, ended);
}

} // namespace

void Repick(Actor &actor) {
    if (Active(actor))
        Pick(actor, JumpEndLock(actor), false);
}

void OnMove(Actor &actor, Move move) {
    if (!Active(actor))
        return;
    switch (move) {
    case Move::StartForward:
    case Move::StartBackward:
    case Move::StartStrafeLeft:
    case Move::StartStrafeRight:
    case Move::StartSwim:
        Casts::EndRangedPose(actor);
        Pick(actor, JumpEndLock(actor), false);
        return;
    case Move::Jump:
        Play(actor, Offsets::ANIM_JUMP_START, false);
        return;
    case Move::RunMode:
    case Move::WalkMode:
        if (actor.mover->flags & 0xF)
            Pick(actor, JumpEndLock(actor), false);
        return;
    case Move::Stop:
    case Move::StopStrafe:
    case Move::StartTurnLeft:
    case Move::StartTurnRight:
    case Move::StopTurn:
    case Move::TeleportAck:
    case Move::StopSwim:
        Pick(actor, JumpEndLock(actor), false);
        return;
    }
}

void OnStep(void *context, uint32_t oldFlags, bool jumped, bool arrived) {
    Actor &actor = *static_cast<Actor *>(context);
    // The arrival's MSG_MOVE_STOP, after the spline's done report.
    if (arrived) {
        actor.movesFinished += 1;
        OnMove(actor, Move::Stop);
    }
    if (!Active(actor))
        return;
    const uint32_t flags = actor.mover->flags;
    // A landing (OnLand 0x00602D00 -> UpdateLandingAnimation 0x00602C60):
    // after a jump or a far fall, JumpEnd standing, JumpLandRun ahead or
    // aside; else, and in the water or backing up, a pick, which a soft
    // landing makes only when the direction changed in the air.
    if (!(flags & Mover::FLAG_FALLING) && (oldFlags & Mover::FLAG_FALLING)) {
        if (!jumped && !(oldFlags & Mover::FLAG_FALLING_FAR)) {
            if ((flags ^ oldFlags) & 0xF)
                Pick(actor, JumpEndLock(actor), false);
        } else if (!(flags & Mover::FLAG_SWIMMING) && (flags & 0xF) == 0) {
            Play(actor, Offsets::ANIM_JUMP_END, false);
        } else if (!(flags & Mover::FLAG_SWIMMING) && !(flags & (Mover::FLAG_BACKWARD | 0x100))) {
            Play(actor, Offsets::ANIM_JUMP_LAND_RUN, false);
        } else {
            Pick(actor, JumpEndLock(actor), false);
        }
    }
    // A fall begins, or turns far (PlayFallAnimation 0x00602C40).
    if (((flags & Mover::FLAG_FALLING) && !(oldFlags & Mover::FLAG_FALLING)) ||
        ((flags & Mover::FLAG_FALLING_FAR) && !(oldFlags & Mover::FLAG_FALLING_FAR))) {
        if (flags & Mover::FLAG_FALLING_FAR)
            Play(actor, Offsets::ANIM_FALL, false);
    }
    // A swim ends: MSG_MOVE_STOP_SWIM, not sent along a spline.
    if (!(flags & Mover::FLAG_SWIMMING) && (oldFlags & Mover::FLAG_SWIMMING) && !Mover::OnSpline(*actor.mover))
        OnMove(actor, Move::StopSwim);
}

void OnBodyTurn(Actor &actor, float turn) {
    Actor::Facings &f = actor.facings;
    f.shuffle = 0;
    if (!Active(actor))
        return;
    const uint32_t flags = actor.mover->flags;
    if ((flags & 0xF) || (flags & Mover::FLAG_FALLING) || (flags & Mover::FLAG_SWIMMING))
        return;
    if (kTurnEpsilon < turn)
        f.shuffle = 1;
    else if (turn < kTurnEpsilonNegative)
        f.shuffle = -1;
    const int current = actor.CurrentAnimation();
    int want;
    if ((flags & Mover::FLAG_TURN_LEFT) || f.shuffle > 0)
        want = Offsets::ANIM_SHUFFLE_LEFT;
    else if ((flags & Mover::FLAG_TURN_RIGHT) || f.shuffle < 0)
        want = Offsets::ANIM_SHUFFLE_RIGHT;
    else if (current == Offsets::ANIM_SHUFFLE_LEFT || current == Offsets::ANIM_SHUFFLE_RIGHT)
        want = Offsets::ANIM_STAND;
    else
        return;
    if (current != want && CanShuffle(actor, JumpEndLock(actor)))
        Pick(actor, JumpEndLock(actor), false);
}

bool FollowOn(Actor &actor) {
    if (!Active(actor))
        return false;
    switch (actor.animation) {
    case Offsets::ANIM_JUMP_START:
    case Offsets::ANIM_JUMP:
        Play(actor, Offsets::ANIM_JUMP, true);
        return true;
    case Offsets::ANIM_FALL:
        Play(actor, Offsets::ANIM_FALL, true);
        return true;
    default: {
        // The rest pick again; JumpEnd's own follow-on clears its lock
        // first, so no pick here sees it. A pick that holds (the body in the
        // air) leaves the ended sequence to repeat, as the model repeats it.
        const uint32_t anim = Select(actor, false);
        Play(actor, anim == kNone ? actor.animation : anim, true);
        return true;
    }
    }
}

} // namespace Model::Scene::Locomotion
