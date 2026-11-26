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

// The gamepad cursor, after the 3.80.2 client (cursor mode 0x141264970, the
// cursor parts of its per-frame update 0x141264B20, MoveCursor 0x14125FB80,
// the visibility pair 0x14125FE00 / 0x14125FE70).
//
// Cursor mode. SetGamePadCursorControl, the Era UI's panel and menu callers
// (CanAutoSetGamePadCursorControl first), the glue screens
// (GamePadCursorOnLogin, forced every frame there), the touchpad and spell
// targeting turn it on; real mouse motion, a mouse button going down, the
// sticks (GamePadCursorAutoDisableSticks), a jump (GamePadCursorAutoDisableJump)
// and a new world turn it off. It never starts while a mouse button holds
// mouse-look or with no connected active device. While it is on, the Cursor
// stick moves the OS cursor and the GamePadCursorLeftClick / RightClick
// buttons click; the sticks that share the Cursor stick's axes (the Camera
// stick by default) do nothing.
//
// The stick moves the real OS cursor, through the engine's own setter
// (SetCursorClientPos), and queues the matching absolute move in the input
// ring, so the next drain hovers what is under the new point before a click
// queued after it. Its speed starts at GamePadCursorSpeedStart and grows by
// GamePadCursorSpeedAccel a second up to GamePadCursorSpeedMax; a frame moves
// `xy * len^2 * speed * dt` of the window WIDTH on both axes, cut to whole
// pixels (3.80.2 drops the remainder too, so a very slow stick does not
// move). Pushed past an edge it turns the camera by
// GamePadCursorPushCamera. The cursor does not move while mouse-look holds
// it (1.12's relative mode, where every warp would turn the camera).
//
// Clicks are the window procedure's own button arms (capture, the live
// cursor point, ring kinds 9 / 0xD), so a pad click is a real click to every
// frame, the world and drag and double-click detection.
//
// Visibility. Under gamepad freelook the cursor is hidden, unless cursor
// mode is on or the mouse moved within GamePadOverlapMouseMs. While hidden
// no frame is hovered and the world mouseover and the targeting circle sit
// at the screen centre (0.5, CursorCenteredYPos), as 3.80.2 points them
// there; 1.12 picks the world only for its hovered frame, so the world frame
// holds the motion focus meanwhile. GamePadCursorCentering with
// GamePadCursorCenteredEmulation puts the clicks on the click buttons under
// freelook too; they click at the OS cursor. (3.80.2 also selects a centred
// variant of the cursor image then; which art it is was not read.)
//
// Casting the spell whose reticle is up again, under gamepad freelook or from
// a pad binding, puts it down where the reticle is (3.80.2's cast wrapper
// 0x1413D4AF0 confirms the targeting). 1.12's spellbook and action-bar casts
// take that spell back instead (StopSpellTargeting); those two calls go here
// first, and commit at the cursor's ground point, the way a left click does,
// which while the cursor is hidden is the screen centre.

#include "gamepad/Cursor.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "event/Custom.h"
#include "frame/MotionFocus.h"
#include "gamepad/Buttons.h"
#include "gamepad/Device.h"
#include "gamepad/GamePad.h"
#include "gamepad/Movement.h"
#include "gamepad/Names.h"
#include "gamepad/State.h"
#include "lua/Arg.h"
#include "object/Resolve.h"
#include "spell/Placement.h"
#include "target/Reason.h"
#include "time/Clock.h"
#include "unit/Position.h"

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace GamePad::Cursor {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kActive = 0.001f;
constexpr uint32_t kWarpFilterMs = 200;
constexpr uint8_t kLeftClick = 4;
constexpr uint32_t kLeftButton = 1, kRightButton = 4;

// --- cvars -------------------------------------------------------------------

struct CVars {
    CVar::Factory::Handle speedStart, speedAccel, speedMax, autoEnable, onLogin, autoDisableSticks,
        autoDisableJump, pushCamera, forTargeting, centeredYPos;
} g_cv;

bool g_centering = false;         // GamePadCursorCentering (0x1446EC840)
bool g_centeredEmulation = true;  // GamePadCursorCenteredEmulation

float F(CVar::Factory::Handle h, float fallback) { return h != nullptr ? CVar::Factory::GetFloat(h) : fallback; }
int I(CVar::Factory::Handle h, int fallback) { return h != nullptr ? CVar::Factory::GetInt(h) : fallback; }

// --- state -------------------------------------------------------------------

bool g_mode = false;           // 0x144C9A405
bool g_touch = false;          // 0x144C9A407
float g_speed = 0.0f;          // 0x1468C00C8
uint32_t g_lastPadMove = 0;    // ic+0x9C: the pad last moved or placed the cursor
float g_cx = 0, g_cy = 0, g_cLen = 0;  // this frame's cursor stick
bool g_hidden = false;         // the last ShowCursor was a hide
bool g_weHid = false;          // the OS cursor is hidden by us
bool g_holding = false;        // the motion focus is held at the centre
float g_savedU = 0, g_savedV = 0;
uint32_t g_prevLookBits = 0;
uint32_t g_heldClicks = 0;     // the mouse buttons the pad holds
bool g_allowHover = true;      // SetAllowHoverEventsWithFreeLook

// --- engine ---------------------------------------------------------------------

uint32_t MouseLookBits() {
    const void *ic = Game::Read<void *>(Offsets::VAR_UI_INPUT_CONTROLLER);
    return ic != nullptr ? Game::Read<uint32_t>(ic, Offsets::OFF_UI_INPUT_FLAGS) & 3 : 0;
}

bool Relative() { return Game::Read<int>(Offsets::VAR_RELATIVE_MOUSE) == 1; }

bool Targeting() { return Game::Read<uint32_t>(Offsets::VAR_SPELL_TARGETING_FLAGS) != 0; }

bool Connected() {
    const Device::Slot *s = Device::SlotByID(ActiveID());
    return s != nullptr && s->connected;
}

HWND Window() {
    using GetWindow_t = HWND(__fastcall *)(int which);
    return reinterpret_cast<GetWindow_t>(static_cast<uintptr_t>(Offsets::FUN_OS_GUI_GET_WINDOW))(0);
}

void ClientSize(int &w, int &h) {
    const RECT r = Game::Read<RECT>(Offsets::VAR_CLIENT_RECT);
    w = r.right - r.left;
    h = r.bottom - r.top;
}

void CursorPos(int &x, int &y) {
    using Get_t = void(__fastcall *)(int *x, int *y);
    reinterpret_cast<Get_t>(static_cast<uintptr_t>(Offsets::FUN_OS_INPUT_GET_CURSOR_POS))(&x, &y);
}

// The engine's setter, and the absolute move the drain hovers with.
void Warp(int x, int y) {
    using Set_t = void(__fastcall *)(int x, int y);
    reinterpret_cast<Set_t>(static_cast<uintptr_t>(Offsets::FUN_OS_INPUT_SET_CURSOR_POS))(x, y);
    using Enqueue_t = void(__fastcall *)(uint32_t kind, uint32_t a, uint32_t b, uint32_t c, uint32_t unused);
    reinterpret_cast<Enqueue_t>(static_cast<uintptr_t>(Offsets::FUN_ENQUEUE_INPUT_EVENT))(
        Offsets::INPUT_RECORD_MOUSE_MOVE, 0, static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0);
}

bool EngineShown() {
    const uint8_t *device = Game::Read<const uint8_t *>(Offsets::VAR_GX_DEVICE);
    return device != nullptr && Game::Read<int>(device, Offsets::OFF_GXDEV_CURSOR_SHOWN) != 0;
}

void SetEngineShown(bool shown) {
    using Shown_t = void(__fastcall *)(int shown);
    reinterpret_cast<Shown_t>(static_cast<uintptr_t>(Offsets::FUN_SET_CURSOR_SHOWN))(shown ? 1 : 0);
}

float CenteredY() {
    const float y = F(g_cv.centeredYPos, 0.6f);
    return y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
}

// While hidden: the world frame holds the motion focus at the centre, so
// nothing else hovers and the world picks there. With hover events off
// (SetAllowHoverEventsWithFreeLook(false)) nothing holds it, so the world
// does not pick either and no unit is the mouseover.
void Hold(bool on) {
    uint8_t *ctx = Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    if (ctx == nullptr)
        return;
    float *point = &Game::Ref<float>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    if (on) {
        if (!g_holding) {
            g_holding = true;
            g_savedU = point[0];
            g_savedV = point[1];
        }
        point[0] = 0.5f;
        point[1] = CenteredY();
        Frame::MotionFocus::Hold(g_allowHover ? Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR) : nullptr);
    } else if (g_holding) {
        g_holding = false;
        point[0] = g_savedU;
        point[1] = g_savedV;
        Frame::MotionFocus::Release();
    }
}

// Applies the last ShowCursor. Mouse-look hides and shows the cursor itself
// (CameraEnterFreelook / CameraLeaveFreelook) and owns it meanwhile; a hide
// it undid is applied again once it ends.
void ApplyShown() {
    if (MouseLookBits() != 0 || Relative()) {
        g_weHid = false;
        Hold(false);
        return;
    }
    if (g_hidden) {
        if (EngineShown())
            SetEngineShown(false);
        g_weHid = true;
        Hold(true);
    } else {
        if (g_weHid) {
            SetEngineShown(true);
            g_weHid = false;
        }
        Hold(false);
    }
}

// 0x14125FE70.
void ShowCursor(bool show) {
    g_hidden = !show;
    ApplyShown();
}

// 0x141261B00, the gamepad half (the mouse half is CursorFreelookCentering).
bool Centered(uint32_t now) {
    return Movement::FreelookOn() && !g_mode && !Movement::MouseRecent(now) && g_centering;
}

// 0x14125FE00.
bool Visible(uint32_t now) {
    if (Centered(now))
        return false;
    const bool freelook = Movement::FreelookOn();
    if (!freelook && MouseLookBits() == 0)
        return true;
    return g_mode || (freelook && Movement::MouseRecent(now));
}

// 0x141261C10 without the centred cursor image.
void RefreshShown() { ShowCursor(Visible(Time::Clock::NowMs())); }

// 0x141266EF0: the stick's X or Y axis is one of the Cursor stick's.
bool SharesAxis(const Config::Resolved &cfg, int stick) {
    auto bit = [](uint8_t axis) { return axis < Names::kAxisCount ? 1u << axis : 0u; };
    const uint32_t mine = bit(cfg.stickX[stick]) | bit(cfg.stickY[stick]);
    const uint32_t cur = bit(cfg.stickX[Names::Cursor]) | bit(cfg.stickY[Names::Cursor]);
    return (mine & cur) != 0;
}

// 0x14125FB80: `dx, dy` in window widths.
void MoveCursor(uint32_t now, float dx, float dy) {
    if (Relative())
        return;
    int cw, ch;
    ClientSize(cw, ch);
    const float w = std::max(static_cast<float>(cw), 1.0f);
    const float h = std::max(static_cast<float>(ch), 1.0f);
    int curX, curY;
    CursorPos(curX, curY);
    const int rawX = static_cast<int>(w * dx) + curX;
    const int rawY = static_cast<int>(w * dy) + curY;
    const int x = rawX <= 0 ? 1 : std::min(rawX, cw - 1);
    const int y = rawY <= 0 ? 1 : std::min(rawY, ch - 1);
    g_lastPadMove = now != 0 ? now : 1;
    if (x != curX || y != curY)
        Warp(x, y);
    const float push = F(g_cv.pushCamera, 0.0f);
    if (push <= 0.0f || (x == rawX && y == rawY))
        return;
    void *cam = Movement::ActiveCamera();
    if (cam == nullptr || Movement::ActiveMover() == nullptr)
        return;
    using Fov_t = float(__thiscall *)(void *cam);
    const float fov = Game::Read<Fov_t>(Game::Read<const void *>(cam, 0), Offsets::VTBL_CAMERA_FOV)(cam);
    const float yawIn = x != rawX ? static_cast<float>(rawX - x) / w * fov : 0.0f;
    const float pitchIn = y != rawY ? static_cast<float>(rawY - y) / h * fov : 0.0f;
    Movement::MouseLook(1200.0f / kPi * push * yawIn, 480.0f / kPi * push * pitchIn);
}

// --- cvars ---------------------------------------------------------------------

// 0x141263B80, shared by GamePadCursorCentering and CenteredEmulation.
template <bool *Flag> int __fastcall OnCentering(CVar::Factory::Handle, const char *, const char *value, void *) {
    *Flag = value != nullptr && std::atoi(value) != 0;
    if (!Movement::FreelookOn() || g_mode)
        return 1;
    Buttons::AssignClicks(g_centering && g_centeredEmulation);
    RefreshShown();
    return 1;
}

void RegisterCVars() {
    using CVar::Factory::Register;
    constexpr int kCategory = Offsets::CVAR_CATEGORY_DEFAULT;
    g_cv.speedStart =
        Register("GamePadCursorSpeedStart", "Speed of GamePad cursor when it starts moving", kCategory, "0.1");
    g_cv.speedAccel = Register("GamePadCursorSpeedAccel",
                               "Acceleration of GamePad cursor per second as it continues to move", kCategory, "2");
    g_cv.speedMax = Register("GamePadCursorSpeedMax", "Top speed of GamePad cursor movement", kCategory, "1");
    g_cv.autoEnable = Register("GamePadCursorAutoEnable",
                               "Auto enable GamePad cursor control when opening UIs that may need it", kCategory, "1");
    g_cv.onLogin = Register("GamePadCursorOnLogin", "Enable GamePad cursor control on login and character screens",
                            kCategory, "1");
    Register("GamePadCursorCentering", "When using GamePad, center the cursor", kCategory, "0",
             &OnCentering<&g_centering>);
    Register("GamePadCursorCenteredEmulation",
             "When cursor is centered for GamePad movement, also emulate mouse clicks", kCategory, "1",
             &OnCentering<&g_centeredEmulation>);
    g_cv.autoDisableSticks =
        Register("GamePadCursorAutoDisableSticks",
                 "GamePad cursor control will auto-disable on stick input (0=none, 1=movement, 2=movement+cursor)",
                 kCategory, "2");
    g_cv.autoDisableJump = Register("GamePadCursorAutoDisableJump",
                                    "GamePad cursor control will auto-disable when you jump", kCategory, "1");
    g_cv.pushCamera = Register("GamePadCursorPushCamera",
                               "Rate for GamePad controlled cursor to push/turn camera when at edge of window",
                               kCategory, "1");
    g_cv.forTargeting =
        Register("GamePadCursorForTargeting",
                 "Enable GamePad controlled cursor for spell targeting (1=enable, 2=start-at-target)", kCategory, "1");
    g_cv.centeredYPos = Register("CursorCenteredYPos",
                                 "0-1 vertical position of centered cursor/targeting (0 at bottom)", kCategory, "0.6");
}

const Game::GlueModuleAutoRegister _cvars{&RegisterCVars};

// --- IsCursorVisible / GetCursorCenterPosition ------------------------------------
// Retail's (InputDocumentation; 3.80.2 has neither). Visible is the engine's
// own cursor flag, which mouse-look and the gamepad hide both clear. The
// centre is the point the hidden cursor sits at, (0.5, CursorCenteredYPos),
// in GetCursorPosition's units: the engine's GetCursorPosition converts it,
// run with the centre in the mouse position it reads.

int __fastcall Script_IsCursorVisible(void *L) {
    Game::Lua::PushBool(L, EngineShown());
    return 1;
}

int __fastcall Script_GetCursorCenterPosition(void *L) {
    uint8_t *ctx = Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    if (ctx == nullptr) {
        Game::Lua::PushNumber(L, 0);
        Game::Lua::PushNumber(L, 0);
        return 2;
    }
    float *point = &Game::Ref<float>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    const float savedU = point[0];
    const float savedV = point[1];
    point[0] = 0.5f;
    point[1] = CenteredY();
    const int pushed = reinterpret_cast<int(__fastcall *)(void *)>(
        static_cast<uintptr_t>(Offsets::FUN_SCRIPT_GETCURSORPOSITION))(L);
    point[0] = savedU;
    point[1] = savedV;
    return pushed;
}

// C_GamePad.SetAllowHoverEventsWithFreeLook / GetAllowHoverEventsWithFreeLook
// (_forever_ only; no 3.80.2 counterpart): whether the hidden cursor's centre
// point hovers the world under gamepad freelook. A gamepad UI turns it off
// while a menu has its focus, so the centre does not show unit tooltips or
// the mouseover behind the menu. The event fires on a change only.
const Game::Doc::Field kAllowHoverPayload[] = {Game::Doc::Req("enabled", "bool")};
const Game::Doc::Event kAllowHoverChangedDoc{
    "GamePad", "Fires when SetAllowHoverEventsWithFreeLook changes whether gamepad freelook hovers the world.",
    kAllowHoverPayload};
const Event::Custom::AutoReserve _allowHoverChanged{"GAME_PAD_ALLOW_HOVER_EVENTS_WITH_FREE_LOOK_CHANGED",
                                                    &kAllowHoverChangedDoc};

void SetAllowHover(bool allow) {
    if (allow == g_allowHover)
        return;
    g_allowHover = allow;
    if (g_holding)
        Hold(true);
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushBool(L, allow);
    Event::Custom::FireValues(_allowHoverChanged.Slot(), L, top + 1, 1);
    Game::Lua::SetTop(L, top);
}

int __fastcall Script_SetAllowHoverEventsWithFreeLook(void *L) {
    bool enable = false;
    if (!Game::Lua::Arg::Bool(L, 1, "Usage: C_GamePad.SetAllowHoverEventsWithFreeLook(enable)", enable))
        return 0;
    SetAllowHover(enable);
    return 0;
}

int __fastcall Script_GetAllowHoverEventsWithFreeLook(void *L) {
    Game::Lua::PushBool(L, g_allowHover);
    return 1;
}

// A reload: the UI that turned hover off is gone.
void ResetAllowHover() {
    g_allowHover = true;
    if (g_holding)
        Hold(true);
}

const Game::ReloadAutoRegister _reloadAllowHover{&ResetAllowHover};

const Game::Doc::Field kAllowArg[] = {Game::Doc::Req("enable", "bool")};
const Game::Doc::Field kAllowedRet[] = {Game::Doc::Req("enabled", "bool")};
const Game::Doc::Function kSetAllowHoverEventsWithFreeLook{
    "Sets whether the hidden cursor's centre point hovers the world (the mouseover unit, unit tooltips) under "
    "gamepad freelook. On by default.",
    kAllowArg, {}, "GamePad"};
const Game::Doc::Function kGetAllowHoverEventsWithFreeLook{
    "Returns whether the hidden cursor's centre point hovers the world under gamepad freelook.", {}, kAllowedRet,
    "GamePad"};

const Game::Doc::Field kVisibleRet[] = {Game::Doc::Req("visible", "bool")};
const Game::Doc::Function kIsCursorVisible{"Returns whether the cursor is shown.", {}, kVisibleRet, "Input"};
const Game::Doc::Field kCenterRets[] = {
    Game::Doc::Req("posX", "number"),
    Game::Doc::Req("posY", "number"),
};
const Game::Doc::Function kGetCursorCenterPosition{
    "Returns where the centred cursor sits, the screen's middle at CursorCenteredYPos, in GetCursorPosition's "
    "units.",
    {}, kCenterRets, "Input"};

void RegisterGlueLua() {
    Game::Lua::RegisterGlueFunction("IsCursorVisible", &Script_IsCursorVisible, &kIsCursorVisible);
    Game::Lua::RegisterGlueFunction("GetCursorCenterPosition", &Script_GetCursorCenterPosition,
                                    &kGetCursorCenterPosition);
}

void RegisterWorldLua() {
    Game::Lua::RegisterTableFunction("C_GamePad", "SetAllowHoverEventsWithFreeLook",
                                     &Script_SetAllowHoverEventsWithFreeLook, &kSetAllowHoverEventsWithFreeLook);
    Game::Lua::RegisterTableFunction("C_GamePad", "GetAllowHoverEventsWithFreeLook",
                                     &Script_GetAllowHoverEventsWithFreeLook, &kGetAllowHoverEventsWithFreeLook);
    Game::Lua::RegisterGlobalFunction("IsCursorVisible", &Script_IsCursorVisible, &kIsCursorVisible);
    Game::Lua::RegisterGlobalFunction("GetCursorCenterPosition", &Script_GetCursorCenterPosition,
                                      &kGetCursorCenterPosition);
}

const Game::GlueModuleAutoRegister _glueLua{&RegisterGlueLua};
const Game::ModuleAutoRegister _worldLua{&RegisterWorldLua};

// --- jump (0x14125C5F0's head) -----------------------------------------------------

using Jump_t = int(__fastcall *)(void *L);
Jump_t g_origJump = nullptr;

int __fastcall Jump_h(void *L) {
    if (g_mode && !Targeting() && I(g_cv.autoDisableJump, 1) != 0) {
        Mode(false, false);
        Movement::SetFreelook(true);
    }
    return g_origJump(L);
}

const Game::HookAutoRegister _jumpHook{Offsets::FUN_SCRIPT_JUMP, reinterpret_cast<void *>(&Jump_h),
                                       reinterpret_cast<void **>(&g_origJump)};

// --- the second cast places the reticle ---------------------------------------------

// In place of the two casts' `call FUN_STOP_PLACEMENT`, which takes no
// arguments.
void __cdecl TakeBackOrPlace() {
    if ((Movement::FreelookOn() || Buttons::InPadBinding()) && Spell::Placement::CommitAtCursor())
        return;
    Spell::Placement::Stop();
}

void RegisterRecast() {
    Common::RedirectCall(Offsets::CALL_CAST_BOOK_SPELL_STOP_PLACEMENT, Offsets::FUN_STOP_PLACEMENT,
                         reinterpret_cast<const void *>(&TakeBackOrPlace));
    Common::RedirectCall(Offsets::CALL_USE_ACTION_STOP_PLACEMENT, Offsets::FUN_STOP_PLACEMENT,
                         reinterpret_cast<const void *>(&TakeBackOrPlace));
}

const Game::ModuleAutoRegister _recast{&RegisterRecast};

// --- Lua -----------------------------------------------------------------------------

int __fastcall Script_CanGamePadControlCursor(void *L) {
    Game::Lua::PushBool(L, Connected());
    return 1;
}

int __fastcall Script_CanAutoSetGamePadCursorControl(void *L) {
    const bool autoEnable = I(g_cv.autoEnable, 0) != 0;
    const bool on = Game::Lua::ToBoolean(L, 1) != 0
                        ? autoEnable && Movement::FreelookOn() && !g_mode
                        : autoEnable && g_mode;
    Game::Lua::PushBool(L, on);
    return 1;
}

int __fastcall Script_IsGamePadCursorControlEnabled(void *L) {
    Game::Lua::PushBool(L, g_mode);
    return 1;
}

int __fastcall Script_SetGamePadCursorControl(void *L) {
    Mode(Game::Lua::ToBoolean(L, 1) != 0, false);
    return 0;
}

const Game::Doc::Field kEnableArg[] = {Game::Doc::Req("enable", "bool")};
const Game::Doc::Field kCanRets[] = {Game::Doc::Req("canControl", "bool")};
const Game::Doc::Field kCanAutoRets[] = {Game::Doc::Req("canAutoSet", "bool")};
const Game::Doc::Field kEnabledRets[] = {Game::Doc::Req("enabled", "bool")};
const Game::Doc::Function kCanGamePadControlCursor{"Returns whether the active gamepad device is connected.", {},
                                                   kCanRets, "InputGlobals"};
const Game::Doc::Function kCanAutoSetGamePadCursorControl{
    "Returns whether the interface may turn gamepad cursor control on (true) or off (false) by itself.",
    kEnableArg, kCanAutoRets, "InputGlobals"};
const Game::Doc::Function kIsGamePadCursorControlEnabled{"Returns whether gamepad cursor control is on.", {},
                                                         kEnabledRets, "InputGlobals"};
const Game::Doc::Function kSetGamePadCursorControl{"Turns gamepad cursor control on or off.", kEnableArg, {},
                                                   "InputGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("CanGamePadControlCursor", &Script_CanGamePadControlCursor,
                                      &kCanGamePadControlCursor);
    Game::Lua::RegisterGlobalFunction("CanAutoSetGamePadCursorControl", &Script_CanAutoSetGamePadCursorControl,
                                      &kCanAutoSetGamePadCursorControl);
    Game::Lua::RegisterGlobalFunction("IsGamePadCursorControlEnabled", &Script_IsGamePadCursorControlEnabled,
                                      &kIsGamePadCursorControlEnabled);
    Game::Lua::RegisterGlobalFunction("SetGamePadCursorControl", &Script_SetGamePadCursorControl,
                                      &kSetGamePadCursorControl);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Mode(bool enable, bool touch) {
    const bool on = enable && MouseLookBits() == 0 && Connected();
    if (on == g_mode && touch == g_touch)
        return;
    g_touch = on && touch;
    const bool clicks = (on && !touch) || (Movement::FreelookOn() && g_centering && g_centeredEmulation);
    g_speed = 0.0f;
    g_mode = on;
    Buttons::AssignClicks(clicks);
    RefreshShown();
}

bool ModeOn() { return g_mode; }
bool TouchMode() { return g_touch; }

bool ClicksWanted() {
    return (g_mode && !g_touch) || (Movement::FreelookOn() && g_centering && g_centeredEmulation);
}

void Click(uint8_t code, bool down) {
    const uint32_t bit = code == kLeftClick ? kLeftButton : kRightButton;
    if (down == ((g_heldClicks & bit) != 0))
        return;
    HWND hwnd = Window();
    if (hwnd == nullptr)
        return;
    g_heldClicks ^= bit;
    const UINT msg = code == kLeftClick ? (down ? WM_LBUTTONDOWN : WM_LBUTTONUP)
                                        : (down ? WM_RBUTTONDOWN : WM_RBUTTONUP);
    using Arm_t = int(__fastcall *)(HWND hwnd, UINT msg, WPARAM wParam, LRESULT *out);
    LRESULT out = 0;
    reinterpret_cast<Arm_t>(static_cast<uintptr_t>(down ? Offsets::FUN_WINDOW_MOUSE_BUTTON_DOWN
                                                        : Offsets::FUN_WINDOW_MOUSE_BUTTON_UP))(hwnd, msg, 0, &out);
}

void ReleaseClicks() {
    if ((g_heldClicks & kLeftButton) != 0)
        Click(kLeftClick, false);
    if ((g_heldClicks & kRightButton) != 0)
        Click(kLeftClick + 1, false);
}

void Begin(const State::Mapped &m, bool haveCamera) {
    // A mouse button going down ends cursor mode (3.80.2's command setter
    // 0x14125F420 on `ic & 3` from 0 to non-0).
    // Its release refreshes the cursor (the group end's 0x141261C10).
    const uint32_t look = MouseLookBits();
    if (look != 0 && g_prevLookBits == 0)
        Mode(false, false);
    else if (look == 0 && g_prevLookBits != 0)
        RefreshShown();
    g_prevLookBits = look;

    // D: the glue screens and the touchpad pick the mode; in the world it
    // stays as it is. (1.12 runs no frame behind a loading screen.)
    const bool touch = std::fabs(m.touchDelta[0] * m.touchDelta[0] + m.touchDelta[1] * m.touchDelta[1]) >=
                       2.3841858e-07f;
    if (!haveCamera || touch) {
        if (touch)
            Mode(true, true);
        else
            Mode(I(g_cv.onLogin, 0) != 0, false);
    }

    // E: the cursor stick; on the glue screens the Movement stick too when it
    // is the longer one.
    g_cx = g_cy = g_cLen = 0.0f;
    if (g_mode && !g_touch) {
        if (Buttons::StickUnclaimed(Names::Cursor)) {
            const State::StickValue &c = m.sticks[Names::Cursor];
            g_cx = c.x;
            g_cy = -c.y;
            g_cLen = c.len;
        }
        const State::StickValue &mv = m.sticks[Names::Movement];
        if (!haveCamera && Buttons::StickUnclaimed(Names::Movement) && g_cLen < mv.len) {
            g_cx = mv.x;
            g_cy = -mv.y;
            g_cLen = mv.len;
        }
    }
}

void AutoDisable(bool moveActive, bool camActive) {
    const int v = I(g_cv.autoDisableSticks, 0);
    if (!g_mode || Targeting() || v == 0)
        return;
    if (v == 1) {
        if (moveActive)
            Mode(false, false);
    } else if ((moveActive || camActive) && ((g_cLen > kActive && !g_touch) || camActive)) {
        Mode(false, false);
    }
}

void End(const State::Mapped &m, float dt, bool moveActive, bool camActive) {
    const uint32_t now = Time::Clock::NowMs();
    if (!g_mode) {
        // Cursor mode just ended by a stick: hide at once (G's first arm).
        const Config::Resolved &cfg = State::ConfigOf(ActiveID());
        if (g_lastPadMove != 0 && Time::Clock::Elapsed(g_lastPadMove, now) < kWarpFilterMs &&
            ((camActive && SharesAxis(cfg, Names::Camera)) || (moveActive && SharesAxis(cfg, Names::Movement))))
            ShowCursor(false);
    } else {
        if (std::fabs(m.touchDelta[0] * m.touchDelta[0] + m.touchDelta[1] * m.touchDelta[1]) >= 2.3841858e-07f)
            MoveCursor(now, m.touchDelta[0], m.touchDelta[1]);
        if (g_cLen <= kActive) {
            g_speed = 0.0f;
        } else {
            if (g_speed <= 0.0f)
                g_speed = F(g_cv.speedStart, 0.1f);
            else
                g_speed = std::min(g_speed + F(g_cv.speedAccel, 1.0f) * dt, F(g_cv.speedMax, 1.0f));
            const float k = g_cLen * g_cLen * g_speed * dt;
            MoveCursor(now, g_cx * k, g_cy * k);
        }
    }
    ApplyShown(); // again: mouse-look may have shown it
}

bool FiltersMotion(uint32_t now) {
    return g_lastPadMove != 0 && Time::Clock::Elapsed(g_lastPadMove, now) < kWarpFilterMs;
}

void OnMouseMotion() {
    if (g_hidden && Visible(Time::Clock::NowMs()))
        ShowCursor(true);
}

void Refresh() { RefreshShown(); }

void OnFreelookChanged() {
    RefreshShown();
    Buttons::AssignClicks(ClicksWanted());
}

void OnPadTargeting() {
    if (I(g_cv.forTargeting, 0) == 0 || g_mode)
        return;
    // The selection itself, as 3.80.2 reads its hard-target slot: a
    // SoftTargetForce target counts, though the `target` token hides it.
    const uint64_t guid = Target::Reason::Selection();
    void *target = guid != 0 ? Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) : nullptr;
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    float pos[3], screen[2];
    if (target == nullptr || worldFrame == nullptr || !Unit::Position::Read(target, pos))
        return;
    using ScreenCoords_t = bool(__thiscall *)(void *worldFrame, const float world[3], float screen[2]);
    if (!reinterpret_cast<ScreenCoords_t>(static_cast<uintptr_t>(Offsets::FUN_WORLDFRAME_GET_SCREEN_COORDS))(
            worldFrame, pos, screen))
        return;
    Mode(true, false);
    int cw, ch;
    ClientSize(cw, ch);
    const float k1 = Game::Read<float>(Offsets::VAR_COORD_DDC_WIDTH_SCALE);
    const float k2 = Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE);
    if (k1 <= 0.0f || k2 <= 0.0f || Relative())
        return;
    Warp(static_cast<int>(screen[0] / k1 * static_cast<float>(cw)),
         static_cast<int>((1.0f - screen[1] / k2) * static_cast<float>(ch)));
    if (g_mode) {
        const uint32_t now = Time::Clock::NowMs();
        g_lastPadMove = now != 0 ? now : 1;
    }
}

void Reset() {
    ReleaseClicks();
    g_mode = g_touch = false;
    g_speed = 0.0f;
    g_lastPadMove = 0;
    g_prevLookBits = 0;
    Buttons::AssignClicks(false);
    ShowCursor(true);
}

} // namespace GamePad::Cursor
