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

// The sticks moving the character and the camera, after the 3.80.2 client's
// per-frame update 0x141264B20.
//
// Movement. The Movement stick (when no frame took it) presses the engine's
// own movement commands, as a second keyboard would: CInputControl::
// SetCommands (FUN_INPUT_SET_COMMANDS) on the edges of what the stick wants,
// one bit per call, releases first. The command word is a plain mask, so the
// stick and a key on the same command behave like two keys bound to it, as
// they do in 3.80.2 (its setter 0x141261040 on the same word). The stick's
// angle `a` (0 up, positive left) picks the bits:
//   GamePadAnalogMovement 0, 8 directions: |a| <= 30 forward, <= 67.5
//     forward + strafe, <= 125 strafe, < 155 back + strafe, else back; the
//     character faces the camera while the stick is held.
//   GamePadAnalogMovement 1: the character faces the stick's direction and
//     runs forward, except in combat (GamePadFaceMovementMaxAngleCombat), where
//     a stick within the angle strafes or back-pedals facing the camera.
// The stick's length picks walk or run at GamePadRunThreshold
// (CGUnit_C::SetRunMode); GamePadTankTurnSpeed turns the character like a
// tank instead.
//
// Camera. The Camera stick turns the camera as mouse-look does: its rate in
// mouse-look units into CCamera::MouseTurn (3.80.2 feeds its
// CGCamera_ApplyMouseLook 0x141B621A0). Full stick turns
// GamePadCameraYawSpeed * cameraYawMoveSpeed * 1.25 degrees a second (225 at
// the defaults) and GamePadCameraPitchSpeed * cameraPitchMoveSpeed (90). The
// Look stick offsets the camera by up to GamePadCameraLookMaxYaw / Pitch
// degrees and springs back. GamePadTurnWithCamera turns the character to the
// camera when the camera stick moves it (1 while moving or in combat, 2
// always).
//
// Freelook. Using a stick or holding a pad button turns gamepad freelook on;
// real mouse motion GamePadOverlapMouseMs after the last pad input turns it
// off, and the character first turns to the camera. GAME_PAD_ACTIVE_CHANGED
// fires on each change. While it is on, the camera holds one count of its
// absolute frame (CCamera::EnterAbsoluteFrame, which mouse-look and fear
// use), so its yaw is the world direction the character is turned to, as
// 3.80.2's freelook puts the camera under user control (0x141B62100).
// Entering 1.12's own mouse-look mode is not an option: it hides the cursor
// and the nameplates and belongs to the mouse buttons.

#include "gamepad/Movement.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "gamepad/Buttons.h"
#include "gamepad/Device.h"
#include "gamepad/GamePad.h"
#include "gamepad/Names.h"
#include "gamepad/State.h"
#include "input/EventBus.h"
#include "object/Resolve.h"
#include "tick/FrameTick.h"
#include "time/Clock.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>

namespace GamePad::Movement {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDegToRad = 0.017453292f;
constexpr float kActive = 0.001f;
constexpr float kMaxPitch = 1.553343f; // the camera's 89 degrees

constexpr uint32_t kForward = 0x10, kBack = 0x20, kStrafeLeft = 0x40, kStrafeRight = 0x80;
constexpr uint32_t kMovementBits = kForward | kBack | kStrafeLeft | kStrafeRight;

const Game::Doc::Field kActivePayload[] = {
    Game::Doc::Req("isActive", "bool", "True when gamepad freelook turned on."),
};
const Game::Doc::Event kActiveChangedDoc{
    "GamePad", "Fires when gamepad freelook turns on (a stick or a held pad button) or off (the mouse moves).",
    kActivePayload};
const Event::Custom::AutoReserve _activeChanged{"GAME_PAD_ACTIVE_CHANGED", &kActiveChangedDoc};

// --- cvars --------------------------------------------------------------------

struct CVars {
    CVar::Factory::Handle analog, runThreshold, turnWithCamera, faceAngle, faceAngleCombat, tankTurn, yawSpeed,
        pitchSpeed, lookMaxPitch, lookMaxYaw, overlapMouse;
} g_cv;

float F(CVar::Factory::Handle h, float fallback) { return h != nullptr ? CVar::Factory::GetFloat(h) : fallback; }
int I(CVar::Factory::Handle h, int fallback) { return h != nullptr ? CVar::Factory::GetInt(h) : fallback; }

float EngineFloat(const char *name) {
    CVar::Factory::Handle h = CVar::Factory::Find(name);
    return h != nullptr ? CVar::Factory::GetFloat(h) : 0.0f;
}

void RegisterCVars() {
    using CVar::Factory::Register;
    constexpr int kDefault = Offsets::CVAR_CATEGORY_DEFAULT;
    constexpr int kGame = Offsets::CVAR_CATEGORY_GAME;
    g_cv.analog = Register("GamePadAnalogMovement",
                           "Enable analog movement in any direction, rather than just the 8 cardinal directions",
                           kDefault, "0");
    g_cv.runThreshold = Register("GamePadRunThreshold",
                                 "0-1 Amount of stick movement before character transitions from walk to run",
                                 kDefault, "0.5");
    g_cv.turnWithCamera = Register(
        "GamePadTurnWithCamera", "Turn character to match when camera facing is changed (1=in-combat, 2=always)",
        kDefault, "1");
    g_cv.faceAngle = Register("GamePadFaceMovementMaxAngle",
                              "Max movement to camera angle to face movement direction instead of camera "
                              "direction. 0 = always, 180 = never (115 allows using strafe with quick turn around)",
                              kDefault, "0");
    g_cv.faceAngleCombat = Register(
        "GamePadFaceMovementMaxAngleCombat",
        "Max movement to camera angle to face movement direction instead of camera direction, in combat. 0 = "
        "always, 180 = never (115 allows using strafe with quick turn around)",
        kDefault, "180");
    g_cv.tankTurn = Register("GamePadTankTurnSpeed", "If non-zero, character turns like a tank from GamePad movement",
                             kDefault, "0");
    g_cv.yawSpeed = Register("GamePadCameraYawSpeed", "Yaw speed of GamePad camera turning left/right", kGame, "1");
    g_cv.pitchSpeed = Register("GamePadCameraPitchSpeed", "Pitch speed of GamePad camera moving up/down", kGame, "1");
    g_cv.lookMaxPitch = Register("GamePadCameraLookMaxPitch", "Max pitch 'Look' stick can adjust camera angle",
                                 kDefault, "0");
    g_cv.lookMaxYaw = Register("GamePadCameraLookMaxYaw", "Max yaw 'Look' stick can adjust camera angle", kDefault, "0");
    g_cv.overlapMouse = Register("GamePadOverlapMouseMs",
                                 "Duration after gamepad+mouse input to switch to just one or the other.", kDefault,
                                 "2000");
}

const Game::GlueModuleAutoRegister _cvars{&RegisterCVars};

// --- engine ---------------------------------------------------------------------

void *Camera() {
    const uint8_t *world = Game::Read<const uint8_t *>(Offsets::VAR_GAME_STATE_PTR);
    return world != nullptr ? Game::Read<void *>(world, Offsets::OFF_GAME_STATE_CAMERA_PTR) : nullptr;
}

void *Mover() {
    if (Game::Read<uint32_t>(Offsets::VAR_PLAYER_HAS_CONTROL) == 0)
        return nullptr;
    const uint64_t guid = Game::Read<uint64_t>(Offsets::VAR_ACTIVE_MOVER_GUID);
    return guid != 0 ? Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) : nullptr;
}

void *InputControl() { return Game::Read<void *>(Offsets::VAR_UI_INPUT_CONTROLLER); }

bool Predicate(uintptr_t fn, void *unit) {
    return unit != nullptr && reinterpret_cast<int(__stdcall *)(void *)>(fn)(unit) != 0;
}

uint32_t MoveFlags(const void *unit) {
    const uint8_t *mv = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    return mv != nullptr ? Game::Read<uint32_t>(mv, Offsets::OFF_MOVEMENT_FLAGS) : 0;
}

float Facing(const void *unit) {
    const uint8_t *mv = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    return mv != nullptr ? Game::Read<float>(mv, Offsets::OFF_MOVEMENT_FACING) : 0.0f;
}

uint32_t UnitFlags(const void *unit) {
    const uint8_t *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_OBJECT_FIELDS);
    return fields != nullptr ? Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) : 0;
}

void SetCommand(uint32_t bit, bool press, uint32_t now) {
    void *ic = InputControl();
    if (ic == nullptr)
        return;
    using SetCommands_t = void(__fastcall *)(void *ic, void *edx, uint32_t bits, uint32_t press, uint32_t time,
                                             uint32_t extra);
    reinterpret_cast<SetCommands_t>(static_cast<uintptr_t>(Offsets::FUN_INPUT_SET_COMMANDS))(
        ic, nullptr, bit, press ? 1 : 0, now, 0);
}

void SetFacing(void *unit, uint32_t now, float facing) {
    facing = std::fmod(facing, 2.0f * kPi);
    if (facing < 0.0f)
        facing += 2.0f * kPi;
    if (std::fabs(facing - Facing(unit)) < 1e-4f)
        return; // still: no event to queue
    using SetFacing_t = void(__fastcall *)(void *unit, void *edx, uint32_t time, float facing);
    reinterpret_cast<SetFacing_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_SET_FACING))(unit, nullptr, now, facing);
}

void HandOffPitch(uint32_t now, float pitch) {
    void *ic = InputControl();
    if (ic == nullptr)
        return;
    pitch = pitch < -kMaxPitch ? -kMaxPitch : (pitch > kMaxPitch ? kMaxPitch : pitch);
    using HandOff_t = void(__fastcall *)(void *ic, void *edx, uint32_t time, float pitch);
    reinterpret_cast<HandOff_t>(static_cast<uintptr_t>(Offsets::FUN_INPUT_HAND_OFF_PITCH))(ic, nullptr, now, pitch);
}

// The camera's pitch as CommitFacing hands it to the body: the live pitch
// plus the pivot bias.
float CameraPitch(const void *cam) {
    return Game::Read<float>(cam, Offsets::OFF_CAMERA_PITCH) + Game::Read<float>(cam, 0x104);
}

float CameraYaw(const void *cam) { return Game::Read<float>(cam, Offsets::OFF_CAMERA_RELATIVE_YAW); }

void CameraCall(uintptr_t fn, void *cam) { reinterpret_cast<void(__fastcall *)(void *)>(fn)(cam); }

float WrapPi(float a) {
    a = std::fmod(a + kPi, 2.0f * kPi);
    if (a < 0.0f)
        a += 2.0f * kPi;
    return a - kPi;
}

// --- state --------------------------------------------------------------------------

uint32_t g_owned = 0;           // the movement bits the stick pressed (0x1468C00D0)
bool g_freelook = false;
void *g_frameCamera = nullptr;  // the camera our absolute-frame count is on
uint32_t g_lastPadInput = 0;    // 0x1468C00CC
uint32_t g_lastMouseInput = 0;
float g_prevLookYaw = 0.0f, g_prevLookPitch = 0.0f;
uint32_t g_pitchReadyAt = 0;
bool g_runModeSent = false;     // a SetRunMode the movement step has not applied yet
bool g_runModeWanted = true;

void FireActiveChanged(bool on) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushBool(L, on);
    Event::Custom::FireValues(_activeChanged.Slot(), L, top + 1, 1);
    Game::Lua::SetTop(L, top);
}

// Keeps the absolute-frame count on the camera that exists now; a camera
// rebuilt (world entry) has no count of ours to take off.
void HoldFrame(bool hold) {
    void *cam = Camera();
    if (g_frameCamera != nullptr && g_frameCamera != cam)
        g_frameCamera = nullptr;
    if (hold && g_frameCamera == nullptr && cam != nullptr) {
        CameraCall(Offsets::FUN_CAMERA_ENTER_ABSOLUTE_FRAME, cam);
        g_frameCamera = cam;
    } else if (!hold && g_frameCamera != nullptr) {
        CameraCall(Offsets::FUN_CAMERA_LEAVE_ABSOLUTE_FRAME, g_frameCamera);
        g_frameCamera = nullptr;
    }
}

void SetRun(void *unit, uint32_t now, bool run) {
    if (g_runModeSent && g_runModeWanted == run)
        return;
    using SetRunMode_t = void(__fastcall *)(void *unit, void *edx, uint32_t time, int run);
    reinterpret_cast<SetRunMode_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_SET_RUN_MODE))(unit, nullptr, now,
                                                                                            run ? 1 : 0);
    g_runModeSent = true;
    g_runModeWanted = run;
}

void Freelook(bool on) {
    if (on == g_freelook)
        return;
    g_freelook = on;
    HoldFrame(on);
    if (!on) {
        // 3.80.2's freelook end runs again (0x1412648B0).
        void *unit = Mover();
        if (unit != nullptr && (MoveFlags(unit) & Offsets::MOVEFLAG_WALK_MODE) != 0 &&
            Predicate(Offsets::FUN_MAY_TRANSLATE, unit))
            SetRun(unit, Time::Clock::NowMs(), true);
    }
    FireActiveChanged(on);
}

bool InCombat(const void *unit) {
    return (UnitFlags(unit) & (Offsets::UNIT_FLAG_IN_COMBAT | Offsets::UNIT_FLAG_PET_IN_COMBAT)) != 0;
}

// 0x14125E9E0: whether the character turns with the camera.
bool TurnsWithCamera(void *unit) {
    if (!g_freelook || !Predicate(Offsets::FUN_MAY_TURN, unit))
        return false;
    const int v = I(g_cv.turnWithCamera, 1);
    if (v <= 0)
        return false;
    if (v != 1)
        return true;
    const uint32_t held = Game::Read<uint32_t>(InputControl(), Offsets::OFF_UI_INPUT_FLAGS);
    return (held & (kMovementBits | 0x1000)) != 0 || ((held & 1) != 0 && (held & 0x302) != 0) ||
           (UnitFlags(unit) & Offsets::UNIT_FLAG_PET_IN_COMBAT) != 0;
}

// 0x141B62770: the character takes the camera's yaw (and pitch).
void TurnWithCamera(void *unit, void *cam, uint32_t now, bool alsoPitch) {
    if (cam == nullptr || g_frameCamera != cam)
        return; // the yaw is the world direction only in the absolute frame
    SetFacing(unit, now, CameraYaw(cam));
    if (alsoPitch)
        HandOffPitch(now, CameraPitch(cam));
}

void ReleaseOwned(uint32_t now) {
    for (uint32_t bit = kForward; bit <= kStrafeRight; bit <<= 1)
        if ((g_owned & bit) != 0)
            SetCommand(bit, false, now);
    g_owned = 0;
}

void ApplyBits(uint32_t bits, uint32_t now) {
    const uint32_t changed = bits ^ g_owned;
    for (uint32_t bit = kForward; bit <= kStrafeRight; bit <<= 1)
        if ((changed & bit) != 0 && (bits & bit) == 0)
            SetCommand(bit, false, now);
    for (uint32_t bit = kForward; bit <= kStrafeRight; bit <<= 1)
        if ((changed & bit) != 0 && (bits & bit) != 0)
            SetCommand(bit, true, now);
    g_owned = bits;
}

// --- mouse --------------------------------------------------------------------------

// 0x14125E3B0: real mouse motion ends freelook once the pad has been idle for
// GamePadOverlapMouseMs, and the character turns to the camera first.
int __fastcall OnMouseMove(const void *, void *) {
    const uint32_t now = Time::Clock::NowMs();
    g_lastMouseInput = now;
    if (!g_freelook)
        return 1;
    const uint32_t overlap = static_cast<uint32_t>(I(g_cv.overlapMouse, 0) > 0 ? I(g_cv.overlapMouse, 0) : 0);
    if (g_lastPadInput != 0 && Time::Clock::Elapsed(g_lastPadInput, now) < overlap)
        return 1;
    void *unit = Mover();
    if (unit != nullptr && TurnsWithCamera(unit))
        TurnWithCamera(unit, Camera(), now, true);
    Freelook(false);
    return 1;
}

bool g_busRegistered = false;

void RegisterWorld() {
    // A new world builds a new camera and resets the input control (3.80.2's
    // 0x14125E6B0 zeroes the owned bits and the look state without events).
    g_owned = 0;
    g_frameCamera = nullptr;
    g_freelook = false;
    g_prevLookYaw = g_prevLookPitch = 0.0f;
    g_runModeSent = false;
    if (!g_busRegistered) {
        g_busRegistered = true;
        Input::EventBus::Register(Offsets::EVENT_ID_MOUSEMOVE, &OnMouseMove, nullptr, FLT_MAX);
        Input::EventBus::Register(Offsets::EVENT_ID_MOUSEMOVE_RELATIVE, &OnMouseMove, nullptr, FLT_MAX);
    }
}

const Game::ModuleAutoRegister _world{&RegisterWorld};

// --- Lua ----------------------------------------------------------------------------

int __fastcall Script_IsGamePadFreelookEnabled(void *L) {
    Game::Lua::PushBool(L, g_freelook);
    return 1;
}

int __fastcall Script_SetGamePadFreeLook(void *L) {
    Freelook(Game::Lua::ToBoolean(L, 1) != 0);
    return 0;
}

bool Recent(uint32_t stamp) {
    const int overlap = I(g_cv.overlapMouse, 0);
    return stamp != 0 && Time::Clock::Elapsed(stamp, Time::Clock::NowMs()) < static_cast<uint32_t>(overlap > 0 ? overlap : 0);
}

int __fastcall Script_IsUsingGamepad(void *L) {
    Game::Lua::PushBool(L, Recent(g_lastPadInput));
    return 1;
}

int __fastcall Script_IsUsingMouse(void *L) {
    Game::Lua::PushBool(L, Recent(g_lastMouseInput));
    return 1;
}

const Game::Doc::Field kEnabledRets[] = {Game::Doc::Req("enabled", "bool")};
const Game::Doc::Field kEnableArgs[] = {Game::Doc::Req("enable", "bool")};
const Game::Doc::Field kUsingRets[] = {Game::Doc::Req("isUsing", "bool")};
const Game::Doc::Function kIsGamePadFreelookEnabled{"Returns whether gamepad freelook is on.", {}, kEnabledRets,
                                                    "InputGlobals"};
const Game::Doc::Function kSetGamePadFreeLook{"Turns gamepad freelook on or off.", kEnableArgs, {}, "InputGlobals"};
const Game::Doc::Function kIsUsingGamepad{
    "Returns whether the gamepad was used within GamePadOverlapMouseMs.", {}, kUsingRets, "InputGlobals"};
const Game::Doc::Function kIsUsingMouse{"Returns whether the mouse moved within GamePadOverlapMouseMs.", {},
                                        kUsingRets, "InputGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsGamePadFreelookEnabled", &Script_IsGamePadFreelookEnabled,
                                      &kIsGamePadFreelookEnabled);
    Game::Lua::RegisterGlobalFunction("SetGamePadFreeLook", &Script_SetGamePadFreeLook, &kSetGamePadFreeLook);
    Game::Lua::RegisterGlobalFunction("IsUsingGamepad", &Script_IsUsingGamepad, &kIsUsingGamepad);
    Game::Lua::RegisterGlobalFunction("IsUsingMouse", &Script_IsUsingMouse, &kIsUsingMouse);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Update() {
    const uint32_t now = Time::Clock::NowMs();
    const float dt = Tick::FrameTick::Elapsed();
    const uint32_t id = ActiveID();
    const Device::Slot *slot = Device::SlotByID(id);
    if (!Started() || slot == nullptr || !slot->connected) {
        // 3.80.2 leaves freelook here (0x141264B20 step A) but keeps the
        // movement it pressed; a device that is gone cannot release it.
        Reset();
        return;
    }
    const State::Mapped &m = State::MappedOf(id);
    void *cam = Camera();
    void *unit = Mover();
    HoldFrame(g_freelook); // follow a rebuilt camera

    const bool canMove = Predicate(Offsets::FUN_MAY_TRANSLATE, unit);
    const bool canTurn = Predicate(Offsets::FUN_MAY_TURN, unit);
    State::StickValue move{}, camStick{}, look{};
    if (unit != nullptr && Buttons::StickUnclaimed(Names::Movement))
        move = m.sticks[Names::Movement];
    if (cam != nullptr) {
        if (Buttons::StickUnclaimed(Names::Camera))
            camStick = m.sticks[Names::Camera];
        look = m.sticks[Names::Look]; // the Look stick ignores the frames
    }
    float lookYaw = 0.0f, lookPitch = 0.0f;
    if (look.len > kActive) {
        const float maxPitch = F(g_cv.lookMaxPitch, 0.0f), maxYaw = F(g_cv.lookMaxYaw, 0.0f);
        if (maxPitch > 0.0f || maxYaw > 0.0f) {
            lookPitch = maxPitch * look.y * kDegToRad;
            lookYaw = maxYaw * kDegToRad * look.x;
        }
    }
    const bool moveActive = move.len > kActive;
    const bool camActive = camStick.len > kActive;
    const bool lookChanged =
        std::fabs(lookYaw - g_prevLookYaw) >= 2.38e-7f || std::fabs(lookPitch - g_prevLookPitch) >= 2.38e-7f;
    const bool steerByMove = unit != nullptr && moveActive && canTurn;
    const bool pitchReady = g_pitchReadyAt == 0 || Time::Clock::Reached(now, g_pitchReadyAt);
    bool pitchUsed = false;

    if (moveActive || camActive || m.buttons != 0) {
        g_lastPadInput = now;
        if (cam != nullptr)
            Freelook(true);
    }

    // Camera (3.80.2's camera block, into MouseTurn's units: a mouse-look
    // step of dx turns dx / K1 * 0.00125 * cameraYawMoveSpeed degrees).
    if (cam != nullptr && (camActive || lookChanged)) {
        float yawRate = 0.0f, pitchRate = 0.0f;
        if (camActive) {
            yawRate = camStick.x * F(g_cv.yawSpeed, 0.0f) * (dt * 1.25f);
            pitchRate = -camStick.y * F(g_cv.pitchSpeed, 0.0f) * dt;
        }
        float dYaw = 0.0f, dPitch = 0.0f;
        if (lookChanged) {
            dYaw = g_prevLookYaw - lookYaw;
            dPitch = lookPitch - g_prevLookPitch;
            const float yms = EngineFloat("cameraYawMoveSpeed");
            const float pms = EngineFloat("cameraPitchMoveSpeed");
            if (yms > 0.0f && pms > 0.0f) {
                dYaw *= 57.29578f / yms;
                dPitch *= 57.29578f / pms;
            }
        }
        const float k1 = Game::Read<float>(Offsets::VAR_COORD_DDC_WIDTH_SCALE);
        const float k2 = Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE);
        using MouseTurn_t = void(__fastcall *)(void *cam, void *edx, float dx, float dy);
        reinterpret_cast<MouseTurn_t>(static_cast<uintptr_t>(Offsets::FUN_CAMERA_MOUSE_TURN))(
            cam, nullptr, (dYaw + yawRate) * 800.0f * k1, (dPitch + pitchRate) * 600.0f * k2);
        g_prevLookYaw = lookYaw;
        g_prevLookPitch = lookPitch;
        if (!steerByMove && unit != nullptr && TurnsWithCamera(unit)) {
            TurnWithCamera(unit, cam, now, pitchReady);
            pitchUsed = pitchReady;
        }
    }

    // Movement.
    if (unit != nullptr) {
        const bool walking = (MoveFlags(unit) & Offsets::MOVEFLAG_WALK_MODE) != 0;
        if (g_runModeSent && walking != g_runModeWanted)
            g_runModeSent = false; // the movement step applied it
        uint32_t bits = 0;
        if (moveActive) {
            const bool wantWalk = move.len < F(g_cv.runThreshold, 0.5f);
            if (canMove && walking != wantWalk)
                SetRun(unit, now, !wantWalk);
            const float tank = F(g_cv.tankTurn, 0.0f);
            if (tank > 0.0f) {
                // 3.80.2's tank turn: X turns the camera and the character
                // faces it, at TankTurnSpeed * 4 rad/s standing, * 2 moving.
                bits = (move.y < -0.2f ? kBack : 0) | (move.y > 0.2f ? kForward : 0);
                if (cam != nullptr && g_frameCamera == cam) {
                    using ShiftYaw_t = void(__fastcall *)(void *cam, void *edx, float delta);
                    reinterpret_cast<ShiftYaw_t>(static_cast<uintptr_t>(Offsets::FUN_CAMERA_SHIFT_YAW))(
                        cam, nullptr, tank * (bits != 0 ? -2.0f : -4.0f) * (move.x * dt));
                    if (canTurn)
                        SetFacing(unit, now, CameraYaw(cam));
                }
            } else {
                const float camYaw = cam != nullptr ? CameraYaw(cam) : Facing(unit);
                float pitch = cam != nullptr ? CameraPitch(cam) : 0.0f;
                const float a = WrapPi(std::atan2(move.y, move.x) - kPi / 2.0f); // 0 up, positive left
                const float A = std::fabs(a);
                const uint32_t strafe = a > 0.0f ? kStrafeLeft : kStrafeRight;
                float target = camYaw;
                if (I(g_cv.analog, 0) == 0) {
                    if (A > 1.1780972f)
                        bits = A > 2.1816616f ? kBack : 0;
                    else
                        bits = kForward;
                    if (A > 0.5235988f && A < 2.7052603f)
                        bits |= strafe;
                } else {
                    const bool combat = InCombat(unit);
                    const float maxA = F(combat ? g_cv.faceAngleCombat : g_cv.faceAngle, 180.0f) * kDegToRad;
                    const bool swimming = (MoveFlags(unit) & Offsets::MOVEFLAG_SWIMMING) != 0;
                    target = camYaw + a;
                    pitch *= move.y;
                    if (A > maxA || !canMove || A <= 0.5235988f || (!combat && swimming)) {
                        bits = kForward; // face the stick, run forward
                    } else if (maxA >= kPi && A > 2.7052603f) {
                        bits = kBack; // back-pedal facing the camera
                        target += kPi;
                    } else {
                        // The strafe band: on the ground the side comes from a
                        // held strafe key, as 3.80.2 reads the command word.
                        const uint32_t held = Game::Read<uint32_t>(InputControl(), Offsets::OFF_UI_INPUT_FLAGS);
                        uint32_t s = 0;
                        if (swimming)
                            s = strafe;
                        // Freelook joins mouse-look's group there: a turn key strafes.
                        const bool looking = (held & 1) != 0 || g_freelook;
                        if ((held & kStrafeLeft) != 0 || (looking && (held & 0x100) != 0))
                            s = kStrafeLeft;
                        else if ((held & kStrafeRight) != 0 || (looking && (held & 0x200) != 0))
                            s = kStrafeRight;
                        const bool fwd = A < 1.1780972f, back = A > 2.1816616f;
                        bits = (back ? kBack : 0) | (fwd ? kForward : 0) | s;
                        const int k = (back ? 1 : 0) - (fwd ? 1 : 0) + 2;
                        target += (s == kStrafeLeft ? 1.0f : s == kStrafeRight ? -1.0f : 0.0f) * (-kPi / 4.0f) *
                                  static_cast<float>(k);
                    }
                }
                if (canTurn && cam != nullptr && g_frameCamera == cam) {
                    SetFacing(unit, now, target);
                    if (pitchReady) {
                        HandOffPitch(now, pitch);
                        pitchUsed = true;
                    }
                }
            }
        } else if ((g_freelook || g_owned != 0) && walking && canMove) {
            SetRun(unit, now, true); // an idle stick under freelook runs again
        }
        ApplyBits(canMove ? bits : 0, now);
    }
    if (pitchUsed)
        g_pitchReadyAt = now + 15;
}

void Reset() {
    ReleaseOwned(Time::Clock::NowMs());
    Freelook(false);
}

} // namespace GamePad::Movement
