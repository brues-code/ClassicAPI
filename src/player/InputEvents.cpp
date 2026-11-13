// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Modern WoW exposes input-state transitions as events; vanilla 1.12
// doesn't. One per-frame `Tick::WorldTick` callback reads the engine
// state once, edge-detects each pair, fires whichever transitioned.
//
//   PLAYER_STARTED_MOVING / STOPPED_MOVING
//     Source: UI-input controller flag bits (WASD / autorun).
//     Key-state based — STOPPED fires the instant the movement keys
//     release, even mid-air. Matches retail.
//
//   PLAYER_STARTED_TURNING / STOPPED_TURNING
//     Source: per-frame delta on the player's body yaw at
//     `CGPlayer + 0x9C4`. Fires when the character is *actually
//     rotating*, not merely on RMB press — matches retail's
//     "camera has moved" semantics. The mouselook bit is held
//     while RMB is down even if the user isn't dragging; gating
//     on it alone would over-fire on every click.
//
//   PLAYER_STARTED_LOOKING / STOPPED_LOOKING
//     Source: same latch model as TURNING, but driven by the
//     camera-relative yaw at `[camera + 0xF0]`. STARTED fires when
//     the free-look bit is held AND the camera yaw changes; STOPPED
//     fires on LMB release.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"

#include <cmath>
#include <cstdint>

namespace Player::InputEvents {

namespace {

const Game::Doc::Event kStartedMovingDoc{
    "Unit", "Fires when the player starts to move with the movement keys, autorun, or both mouse buttons.",
    {}};
const Game::Doc::Event kStoppedMovingDoc{
    "Unit", "Fires when the player releases all movement input, even while still in the air.", {}};
const Game::Doc::Event kStartedLookingDoc{
    "Unit", "Fires when the player first moves the camera while holding the left mouse button.", {}};
const Game::Doc::Event kStoppedLookingDoc{
    "Unit", "Fires when the player releases the left mouse button after moving the camera.", {}};
const Game::Doc::Event kStartedTurningDoc{
    "Unit", "Fires when the player first turns the character while holding the right mouse button.",
    {}};
const Game::Doc::Event kStoppedTurningDoc{
    "Unit", "Fires when the player releases the right mouse button after turning the character.", {}};

const Event::Custom::AutoReserve _reserveStartedMoving{"PLAYER_STARTED_MOVING", &kStartedMovingDoc};
const Event::Custom::AutoReserve _reserveStoppedMoving{"PLAYER_STOPPED_MOVING", &kStoppedMovingDoc};
const Event::Custom::AutoReserve _reserveStartedLooking{"PLAYER_STARTED_LOOKING",
                                                        &kStartedLookingDoc};
const Event::Custom::AutoReserve _reserveStoppedLooking{"PLAYER_STOPPED_LOOKING",
                                                        &kStoppedLookingDoc};
const Event::Custom::AutoReserve _reserveStartedTurning{"PLAYER_STARTED_TURNING",
                                                        &kStartedTurningDoc};
const Event::Custom::AutoReserve _reserveStoppedTurning{"PLAYER_STOPPED_TURNING",
                                                        &kStoppedTurningDoc};

// Below this per-frame delta, treat the yaw as stable. Picks up
// real rotation (smallest user-perceptible RMB-drag is ~0.5°/frame
// = 0.009 rad) while ignoring float drift / single-bit jitter on
// the broadcast-position copy.
constexpr float YAW_EPSILON = 0.001f;

bool g_wasMoving = false;
bool g_wasLooking = false;
bool g_wasTurning = false;

float g_prevBodyYaw = 0.0f;
bool g_havePrevBodyYaw = false;
float g_prevCameraYaw = 0.0f;
bool g_havePrevCameraYaw = false;

const uint8_t *Controller() {
    return Game::Read<const uint8_t *>(Offsets::VAR_UI_INPUT_CONTROLLER);
}

const uint8_t *Player() {
    return static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));
}

const uint8_t *Camera() {
    const uint8_t *gameState = Game::Read<const uint8_t *>(
        Offsets::VAR_GAME_STATE_PTR);
    if (gameState == nullptr)
        return nullptr;
    return Game::Read<const uint8_t *>(
        gameState, Offsets::OFF_GAME_STATE_CAMERA_PTR);
}

void FireTransition(bool now, bool &prev, const Event::Custom::AutoReserve &started,
                    const Event::Custom::AutoReserve &stopped) {
    if (now == prev)
        return;
    prev = now;
    const int slot = (now ? started : stopped).Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "");
}

void OnWorldTick() {
    auto *ctrl = Controller();
    auto *player = Player();
    auto *camera = Camera();
    if (ctrl == nullptr || player == nullptr) {
        // Pre-login / teardown — reset so first valid tick doesn't
        // fire spurious STOPPED transitions.
        g_wasMoving = false;
        g_wasLooking = false;
        g_wasTurning = false;
        g_havePrevBodyYaw = false;
        g_havePrevCameraYaw = false;
        return;
    }

    const uint32_t flags = Game::Read<uint32_t>(ctrl, Offsets::OFF_UI_INPUT_FLAGS);
    const bool freeLookHeld  = (flags & Offsets::INPUT_FLAG_FREE_LOOK) != 0;
    const bool mouselookHeld = (flags & Offsets::INPUT_FLAG_MOUSELOOK) != 0;

    // MOVING — translational input. The WASD / autorun keys set the
    // INPUT_FLAGS_MOVING_ANY bits directly. The "hold both mouse
    // buttons to run forward" gesture does NOT: the engine drives that
    // forward move at the movement layer (FUN_005103e0 from the button
    // handler) and actually *clears* the autorun bit on the both-button
    // press, so no MOVING_ANY bit is ever set. Detect it from the
    // bit combo instead — both mouselook (RMB) and free-look (LMB)
    // held means the engine is mouse-steering the character forward.
    const bool mouseSteerMoving = mouselookHeld && freeLookHeld;
    const bool moving =
        (flags & Offsets::INPUT_FLAGS_MOVING_ANY) != 0 || mouseSteerMoving;

    // TURNING — latched bit-AND-rotation signal:
    //   STARTED fires when mouselook bit is held AND the body yaw
    //     changes (the first actual drag after pressing RMB).
    //   STOPPED fires when the mouselook bit clears (RMB release).
    // Once STARTED has fired during a hold, the latch stays on
    // until the bit clears — so a drag-stop-drag motion within
    // the same RMB hold doesn't flap STARTED/STOPPED.
    const float bodyYaw = Game::Read<float>(player, Offsets::OFF_PLAYER_BODY_YAW);
    bool turning;
    if (!mouselookHeld) {
        turning = false;
    } else if (g_wasTurning) {
        turning = true;
    } else if (g_havePrevBodyYaw &&
               std::fabs(bodyYaw - g_prevBodyYaw) > YAW_EPSILON) {
        turning = true;
    } else {
        turning = false;
    }
    g_prevBodyYaw = bodyYaw;
    g_havePrevBodyYaw = true;

    // LOOKING — same latch model, but driven by camera-relative yaw
    // at `[camera + 0xF0]`. That field stays at 0 during RMB-
    // mouselook (camera rotates *with* the character), so this
    // doesn't double-fire when both buttons are involved.
    bool looking;
    if (!freeLookHeld) {
        looking = false;
    } else if (camera == nullptr) {
        looking = false; // can't read camera yaw — bail
    } else {
        const float cameraYaw = Game::Read<float>(
            camera, Offsets::OFF_CAMERA_RELATIVE_YAW);
        if (g_wasLooking) {
            looking = true;
        } else if (g_havePrevCameraYaw &&
                   std::fabs(cameraYaw - g_prevCameraYaw) > YAW_EPSILON) {
            looking = true;
        } else {
            looking = false;
        }
        g_prevCameraYaw = cameraYaw;
        g_havePrevCameraYaw = true;
    }

    FireTransition(moving,  g_wasMoving,  _reserveStartedMoving,  _reserveStoppedMoving);
    FireTransition(turning, g_wasTurning, _reserveStartedTurning, _reserveStoppedTurning);
    FireTransition(looking, g_wasLooking, _reserveStartedLooking, _reserveStoppedLooking);
}

} // namespace

static const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

} // namespace Player::InputEvents
