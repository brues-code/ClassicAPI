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

// Gamepad buttons and sticks, after the 3.80.2 client.
//
// Each frame (its edge loop 0x14125B710), for the active device: last
// frame's tap ups, then every changed button by bit index with all its edges
// (up to three, rebuilt from the previous, pressed, released and current
// masks), then the sticks. Pad buttons never repeat.
//
// A button down (0x140562D40) goes to the frames that enabled gamepad
// buttons (EnableGamePadButton, or SetScript / HookScript of a gamepad
// button script; an XML script does not enable), visible and with an
// OnGamePadButtonDown or OnGamePadButtonUp script, topmost first (strata,
// then level; frames that tie keep their enable order, as 1.12's input lists
// do). Each runs OnGamePadButtonDown(self, "PAD1"); the first one without
// propagateKeyboardInput takes the button and gets its up, nobody else does.
// A frame with only an up script takes the down without running anything.
// The script's return value does not count: in 3.80.2 only a secure
// handler's `true` passes the button on, and 1.12 has no secure code.
//
// A down no frame took runs the key binding of its chord (3.80.2's
// CInputFrame, 0x1405A0F10): the held modifiers as 1.12's chord builder
// writes them ("ALT-CTRL-SHIFT-", 0x004B6630) and the button name, through
// the binding dispatch FUN_BINDING_KEY_DISPATCH (and so Bindings::Api's
// override bindings, reduced keys and direct actions). When a binding ran,
// the up replays the chord latched at the down, so a binding released after
// its modifier still gets its up (movement stops, SPELL bindings cast), as
// 1.12's world frame replays a key's.
//
// A stick with a length this frame or last frame sends OnGamePadStick(self,
// stick, x, y, len) to the visible frames that enabled sticks and have the
// script, topmost first, until one takes it (the same propagate rule). A
// stick no frame took is "unclaimed" for this frame: the character, the
// camera and the cursor may use it. The Cursor stick is sent only in cursor
// mode, and then (unless the touchpad started it) a stick that shares one of
// its axes is not sent at all.
//
// The buttons named by GamePadEmulateShift / Ctrl / Alt are those modifiers
// (Input::Modifier::Emulate), never PAD keys, except when tapped: released
// within GamePadEmulateTapWindowMs with no other button pressed meanwhile,
// the button is pressed as its PAD key. 3.80.2 sends that press at once, after
// the modifier's release; 1.12 delivers the release at the next tick's input
// drain, so the press waits one frame and its release one more.
//
// The GamePadCursorLeftClick / RightClick buttons click the mouse while the
// cursor clicks are on (GamePad::Cursor), and are plain PAD keys otherwise.
// A touchpad click or tap clicks too, with or without cursor mode.

#include "gamepad/Buttons.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "frame/Destruction.h"
#include "frame/ExtraScripts.h"
#include "frame/Flags.h"
#include "frame/RegionInput.h"
#include "frame/ScriptArgs.h"
#include "frame/ScriptBindings.h"
#include "gamepad/Cursor.h"
#include "gamepad/Device.h"
#include "gamepad/GamePad.h"
#include "gamepad/Names.h"
#include "gamepad/State.h"
#include "input/Modifier.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace GamePad::Buttons {

namespace {

const Frame::ExtraScripts::Script g_onButtonDown{"ongamepadbuttondown", Offsets::VAR_FRAME_LUA_TYPE_ID};
const Frame::ExtraScripts::Script g_onButtonUp{"ongamepadbuttonup", Offsets::VAR_FRAME_LUA_TYPE_ID};
const Frame::ExtraScripts::Script g_onStick{"ongamepadstick", Offsets::VAR_FRAME_LUA_TYPE_ID};

bool HasScript(const Frame::ExtraScripts::Script &script, const void *frame) {
    const uint32_t *slot = script.Slot(frame);
    return slot != nullptr && slot[0] != 0;
}

// A pad script runs as an input event. 3.80.2 marks every input dispatch on
// the frame manager's +0xFD8 for its length: 1 from the key and mouse
// dispatchers, 3 from the pad button (0x140562D40) and stick (0x1401E8B80)
// ones. 1.12's action gate FUN_CURSOR_EDIT_ALLOWED lets addon code cast
// (TryCastSpell asks it with kind 2) only during an input event: a button
// click (VAR_BUTTON_IN_CLICK), an edit box's Enter or a running key binding.
// So the script runs under the click flag, the one input whose only reader is
// that gate; the old value comes back, so a click inside the script cannot
// clear it early.
template <typename Fn> void AsInputEvent(Fn fn) {
    auto &flag = Game::Ref<int>(Offsets::VAR_BUTTON_IN_CLICK);
    const int saved = flag;
    flag = 1;
    fn();
    flag = saved;
}

// --- enabled frames ---------------------------------------------------------

// The frames in enable order, and the sets that answer IsGamePad*Enabled.
struct Kind {
    std::vector<void *> order;
    std::unordered_set<const void *> set;

    void Enable(void *frame, bool on) {
        if (on == (set.count(frame) != 0))
            return;
        if (on) {
            set.insert(frame);
            order.push_back(frame);
        } else {
            set.erase(frame);
            order.erase(std::remove(order.begin(), order.end(), frame), order.end());
        }
    }
    bool Has(const void *frame) const { return set.count(frame) != 0; }
    void Clear() {
        order.clear();
        set.clear();
    }
};

Kind g_buttons;
Kind g_sticks;

// Who took each button's down: a frame, kBinding for the binding fallback
// when a binding ran, or null.
void *const kBinding = reinterpret_cast<void *>(1);
void *g_capture[Names::kButtonCount] = {};
char g_latch[Names::kButtonCount][32] = {};

float g_lastSentLen[Names::kStickCount] = {};
bool g_unclaimed[Names::kStickCount] = {};

bool Visible(const void *frame) { return Game::Read<int>(frame, Offsets::OFF_FRAME_VISIBLE) != 0; }

bool Propagates(const void *frame) { return Frame::Flags::Has(frame, Offsets::FRAME_FLAG_PROPAGATE_KEYBOARD); }

// The visible frames of `kind` that `accept`, topmost first.
template <typename Accept> std::vector<void *> Candidates(const Kind &kind, Accept accept) {
    std::vector<void *> out;
    for (void *f : kind.order)
        if (Visible(f) && accept(f))
            out.push_back(f);
    std::stable_sort(out.begin(), out.end(),
                     [](const void *a, const void *b) { return Frame::RegionInput::Above(a, b); });
    return out;
}

void OnFrameDestroyed(void *frame) {
    g_buttons.Enable(frame, false);
    g_sticks.Enable(frame, false);
    for (void *&c : g_capture)
        if (c == frame)
            c = nullptr;
}

const Frame::Destruction::AutoSubscribe _destroyed{&OnFrameDestroyed};

void PrepareForReload() {
    g_buttons.Clear();
    g_sticks.Clear();
    for (int b = 0; b < Names::kButtonCount; ++b)
        if (g_capture[b] != kBinding)
            g_capture[b] = nullptr;
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

// 3.80.2's SetScript enables the input kind of the script it sets
// (0x1405BDB60, kind 5 for the button scripts, 6 for the stick one).
void OnHandlerSet(void *frame, const char *name) {
    if (name == nullptr)
        return;
    if (_stricmp(name, "OnGamePadButtonDown") == 0 || _stricmp(name, "OnGamePadButtonUp") == 0)
        g_buttons.Enable(frame, true);
    else if (_stricmp(name, "OnGamePadStick") == 0)
        g_sticks.Enable(frame, true);
}

const Frame::ScriptBindings::HandlerSetAutoSubscribe _handlerSet{&OnHandlerSet};

// --- bindings -----------------------------------------------------------------

uint32_t g_pendingModifiers = 0; // set by Emulate this frame, not yet drained
uint32_t g_releasedModifiers = 0;

// The chord of `button` with the held modifiers, as 1.12's chord builder
// writes it. The emulated modifiers changed this frame count already: the
// engine sees them only at the next tick's input drain.
bool BuildChord(uint8_t button, char (&out)[32]) {
    uint32_t mask = Game::Read<uint32_t>(Offsets::VAR_MODIFIER_KEYS) & 7;
    mask = (mask & ~g_releasedModifiers) | g_pendingModifiers;
    char *cursor = out;
    int remaining = sizeof out;
    using Append_t = int(__fastcall *)(uint32_t mask, char **cursor, int *remaining);
    if (!reinterpret_cast<Append_t>(static_cast<uintptr_t>(Offsets::FUN_APPEND_MODIFIER_PREFIXES))(
            mask, &cursor, &remaining))
        return false;
    const char *name = Names::kButtonBinding[button];
    if (static_cast<int>(std::strlen(name)) >= remaining)
        return false;
    std::memcpy(cursor, name, std::strlen(name) + 1);
    return true;
}

bool g_inPadBinding = false;

bool RunBinding(const char *chord, bool down) {
    void *mgr = Game::Read<void *>(Offsets::VAR_BINDINGS_MANAGER);
    if (mgr == nullptr || chord[0] == '\0')
        return false; // no bindings on the login screens
    // 3.80.2 marks the binding as a pad one while it runs (0x144C95BBC), and
    // its cast start places the cursor for the targeting that cast begins.
    const bool targeting = Game::Read<uint32_t>(Offsets::VAR_SPELL_TARGETING_FLAGS) != 0;
    using Dispatch_t = int(__fastcall *)(void *mgr, void *edx, const char *chord, int down);
    const bool outer = g_inPadBinding;
    g_inPadBinding = true;
    const bool ran = reinterpret_cast<Dispatch_t>(static_cast<uintptr_t>(Offsets::FUN_BINDING_KEY_DISPATCH))(
                         mgr, nullptr, chord, down ? 1 : 0) != 0;
    g_inPadBinding = outer;
    if (!targeting && Game::Read<uint32_t>(Offsets::VAR_SPELL_TARGETING_FLAGS) != 0)
        Cursor::OnPadTargeting();
    return ran;
}

// --- button events --------------------------------------------------------------

uint32_t g_tapCandidates = 0;    // emulated functions pressed with no other down since (0x1468C0018)
uint32_t g_tapDowns = 0;         // tapped buttons to press next frame
uint32_t g_tapUps = 0;           // and to release the frame after (0x1468BFFB8)

void ButtonDown(uint8_t b) {
    g_tapCandidates = 0; // any real down ends every tap
    const char *name = Names::kButtonBinding[b];
    const std::vector<void *> list = Candidates(
        g_buttons, [](const void *f) { return HasScript(g_onButtonDown, f) || HasScript(g_onButtonUp, f); });
    for (void *f : list) {
        if (!g_buttons.Has(f) || !Visible(f))
            continue; // an earlier script hid or disabled it
        AsInputEvent([&] {
            Frame::ScriptArgs::RunSlot(g_onButtonDown.Slot(f), f, [&](void *L) {
                Game::Lua::PushString(L, name);
                return 1;
            });
        });
        if (!Propagates(f)) {
            g_capture[b] = f;
            return;
        }
    }
    if (BuildChord(b, g_latch[b]) && RunBinding(g_latch[b], true))
        g_capture[b] = kBinding;
}

void ButtonUp(uint8_t b) {
    void *owner = g_capture[b];
    g_capture[b] = nullptr;
    if (owner == kBinding) {
        RunBinding(g_latch[b], false);
        g_latch[b][0] = '\0';
    } else if (owner != nullptr && g_buttons.Has(owner) && Visible(owner)) {
        const char *name = Names::kButtonBinding[b];
        AsInputEvent([&] {
            Frame::ScriptArgs::RunSlot(g_onButtonUp.Slot(owner), owner, [&](void *L) {
                Game::Lua::PushString(L, name);
                return 1;
            });
        });
    }
}

void SendButton(uint8_t b, bool down) {
    if (down)
        ButtonDown(b);
    else
        ButtonUp(b);
}

// --- emulated modifiers (0x14125B5F0 / 0x14125BB90 / 0x1412637A0) --------------

// Function codes, 3.80.2's: 1 Alt, 2 Ctrl, 3 Shift, 4 / 5 the cursor's left
// and right clicks.
enum Code : uint8_t { kNoCode = 0, kAlt = 1, kCtrl = 2, kShift = 3, kLeftClick = 4, kRightClick = 5, kCodeCount = 6 };

uint8_t g_function[Names::kButtonCount] = {};
uint32_t g_functionMask = 0; // buttons with a function (0x1468BFFE0)
uint32_t g_activeCodes = 0;  // functions held down (0x1468BFFE4)
uint64_t g_tapDeadline[kCodeCount] = {};
uint64_t g_tapWindowNs = 350ull * 1000000ull;

int KeyOf(uint8_t code) { return code == kShift ? 0 : code == kCtrl ? 1 : 2; }

bool IsClick(uint8_t code) { return code == kLeftClick || code == kRightClick; }

// 0x141277010 for one function's edge.
void SetFunction(uint8_t code, bool down) {
    const uint32_t bit = 1u << code;
    if (down == ((g_activeCodes & bit) != 0))
        return;
    g_activeCodes = down ? g_activeCodes | bit : g_activeCodes & ~bit;
    if (IsClick(code)) {
        Cursor::Click(code, down);
        return;
    }
    const uint32_t merged = 1u << KeyOf(code);
    if (down) {
        g_pendingModifiers |= merged;
        g_releasedModifiers &= ~merged;
    } else {
        g_pendingModifiers &= ~merged;
        g_releasedModifiers |= merged;
    }
    Input::Modifier::Emulate(KeyOf(code), down);
}

void RebuildMask() {
    g_functionMask = 0;
    for (int b = 0; b < Names::kButtonCount; ++b)
        if (g_function[b] != kNoCode)
            g_functionMask |= 1u << b;
}

void Assign(uint8_t code, uint8_t button) {
    SetFunction(code, false);
    if (button < Names::kButtonCount && g_function[button] != kNoCode)
        SetFunction(g_function[button], false);
    for (uint8_t &f : g_function)
        if (f == code)
            f = kNoCode;
    if (button < Names::kButtonCount)
        g_function[button] = code;
    RebuildMask();
}

void EmulatedEdge(uint8_t button, uint8_t code, bool down) {
    SetFunction(code, down);
    if (IsClick(code)) {
        if (down)
            g_tapCandidates = 0; // a click down ends every tap
        return;
    }
    const uint64_t now = Device::NowNs();
    const uint32_t bit = 1u << code;
    if (down) {
        g_tapCandidates |= bit;
        g_tapDeadline[code] = now + g_tapWindowNs;
        return;
    }
    if ((g_tapCandidates & bit) != 0 && now < g_tapDeadline[code])
        g_tapDowns |= 1u << button;
    g_tapCandidates &= ~bit;
    g_tapDeadline[code] = 0;
}

void Emit(uint8_t b, bool down) {
    if ((g_functionMask & (1u << b)) != 0)
        EmulatedEdge(b, g_function[b], down);
    else
        SendButton(b, down);
}

CVar::Factory::Handle g_cvLeftClick = nullptr, g_cvRightClick = nullptr;
bool g_clicksAssigned = false; // 0x144C9A406

// 0x1412637A0, shared by the five function cvars. A click cvar applies at
// once only while the clicks are on.
template <uint8_t C> int __fastcall OnEmulate(CVar::Factory::Handle, const char *, const char *value, void *) {
    const uint8_t button = Names::BindingToIndex(value);
    if (button == Names::kNone && (value == nullptr || _stricmp(value, "NONE") != 0)) {
        Game::Console::Write("Valid button bind names:");
        for (int i = 0; i < Names::kButtonCount; i += 4)
            Game::Console::Printf("%s %s %s %s", Names::kButtonBinding[i], Names::kButtonBinding[i + 1],
                                  Names::kButtonBinding[i + 2], Names::kButtonBinding[i + 3]);
        return 0;
    }
    if (!IsClick(C) || Cursor::ClicksWanted())
        Assign(C, button);
    return 1;
}

int __fastcall OnTapWindow(CVar::Factory::Handle, const char *, const char *value, void *) {
    const int ms = value != nullptr ? std::atoi(value) : 0;
    g_tapWindowNs = static_cast<uint64_t>(ms > 0 ? ms : 0) * 1000000ull;
    return 1;
}

void RegisterCVars() {
    using CVar::Factory::Register;
    constexpr int kCategory = Offsets::CVAR_CATEGORY_DEFAULT;
    Register("GamePadEmulateShift", "GamePad button that should emulate the Shift key", kCategory, "PADLTRIGGER",
             &OnEmulate<kShift>);
    Register("GamePadEmulateCtrl", "GamePad button that should emulate the Ctrl key", kCategory, "PADLSHOULDER",
             &OnEmulate<kCtrl>);
    Register("GamePadEmulateAlt", "GamePad button that should emulate the Alt key", kCategory, "none",
             &OnEmulate<kAlt>);
    Register("GamePadEmulateTapWindowMs",
             "GamePad buttons emulating Ctrl/Alt/Shift will be 'tapped' if released withing this time in MS",
             kCategory, "350", &OnTapWindow);
    g_cvLeftClick = Register("GamePadCursorLeftClick",
                             "GamePad button that should emulate mouse Left Click while controlling the mouse cursor",
                             kCategory, "PADRTRIGGER", &OnEmulate<kLeftClick>);
    g_cvRightClick =
        Register("GamePadCursorRightClick",
                 "GamePad button that should emulate mouse Right Click while controlling the mouse cursor", kCategory,
                 "PADRSHOULDER", &OnEmulate<kRightClick>);
}

const Game::GlueModuleAutoRegister _cvars{&RegisterCVars};

// --- sticks (0x1401E8B80) ---------------------------------------------------------

// 0x141266EF0's test: the stick's X or Y axis is one of the Cursor stick's.
bool SharesCursorAxis(const Config::Resolved &cfg, int s) {
    auto bit = [](uint8_t axis) { return axis < Names::kAxisCount ? 1u << axis : 0u; };
    return ((bit(cfg.stickX[s]) | bit(cfg.stickY[s])) &
            (bit(cfg.stickX[Names::Cursor]) | bit(cfg.stickY[Names::Cursor]))) != 0;
}

void SendStick(int s, const State::StickValue &v) {
    if (!Cursor::ModeOn()) {
        if (s == Names::Cursor)
            return;
    } else if (!Cursor::TouchMode() && s != Names::Cursor && SharesCursorAxis(State::ConfigOf(ActiveID()), s)) {
        return;
    }
    // 0x1401E8B80 stops at the first frame's handler (0x1405A1190), which
    // always answers "taken": that frame alone decides. It hands the stick on
    // to movement when its script returned true or it propagates keyboard
    // input (and is not protected, which 1.12 has no notion of).
    const std::vector<void *> list = Candidates(g_sticks, [](const void *f) { return HasScript(g_onStick, f); });
    if (!list.empty()) {
        void *f = list.front();
        bool unhandled = false;
        AsInputEvent([&] {
            Frame::ScriptArgs::RunSlot(
                g_onStick.Slot(f), f,
                [&](void *L) {
                    Game::Lua::PushString(L, Names::kStick[s]);
                    Game::Lua::PushNumber(L, static_cast<double>(v.x));
                    Game::Lua::PushNumber(L, static_cast<double>(v.y));
                    Game::Lua::PushNumber(L, static_cast<double>(v.len));
                    return 4;
                },
                &unhandled);
        });
        if (!unhandled && !Propagates(f))
            return;
    }
    g_unclaimed[s] = true;
}

void ReleaseAll() {
    for (uint8_t b = 0; b < Names::kButtonCount; ++b)
        if (g_capture[b] != nullptr)
            ButtonUp(b);
    for (uint8_t code = kAlt; code < kCodeCount; ++code)
        SetFunction(code, false);
    g_tapCandidates = g_tapDowns = g_tapUps = 0;
    for (float &len : g_lastSentLen)
        len = 0.0f;
    Cursor::ReleaseClicks(); // a touchpad click holds its button outside the functions
}

// --- frame methods ------------------------------------------------------------------

template <Kind *K> int __fastcall Script_Enable(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    K->Enable(frame, Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL || Game::Lua::ToBoolean(L, 2) != 0);
    if (K == &g_buttons && !K->Has(frame))
        for (void *&c : g_capture)
            if (c == frame)
                c = nullptr;
    return 0;
}

template <Kind *K> int __fastcall Script_IsEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, K->Has(frame));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"EnableGamePadButton", &Script_Enable<&g_buttons>},
    {"IsGamePadButtonEnabled", &Script_IsEnabled<&g_buttons>},
    {"EnableGamePadStick", &Script_Enable<&g_sticks>},
    {"IsGamePadStickEnabled", &Script_IsEnabled<&g_sticks>},
};

const Game::Doc::Field kEnableArgs[] = {Game::Doc::Opt("enable", "bool", "true")};
const Game::Doc::Field kEnabledRets[] = {Game::Doc::Req("enabled", "bool")};
const Game::Doc::Function kEnableGamePadButton{
    "Sets whether the frame gets gamepad buttons: OnGamePadButtonDown and OnGamePadButtonUp.", kEnableArgs, {}};
const Game::Doc::Function kIsGamePadButtonEnabled{"Returns whether the frame gets gamepad buttons.", {},
                                                  kEnabledRets};
const Game::Doc::Function kEnableGamePadStick{"Sets whether the frame gets gamepad sticks: OnGamePadStick.",
                                              kEnableArgs, {}};
const Game::Doc::Function kIsGamePadStickEnabled{"Returns whether the frame gets gamepad sticks.", {},
                                                 kEnabledRets};

const Game::Doc::Method g_methodDocs[] = {
    {"EnableGamePadButton", &kEnableGamePadButton},
    {"IsGamePadButtonEnabled", &kIsGamePadButtonEnabled},
    {"EnableGamePadStick", &kEnableGamePadStick},
    {"IsGamePadStickEnabled", &kIsGamePadStickEnabled},
};

// --- bindings: PAD keys -------------------------------------------------------------

// IsBindingForGamePad (0x1412511C0): "PAD" at the start or after a '-'. Only
// the first "PAD" counts, so "NUMPAD1" is not one.
bool IsPadKey(const char *key) {
    const char *p = std::strstr(key, "PAD");
    return p != nullptr && (p == key || p[-1] == '-');
}

int __fastcall Script_IsBindingForGamePad(void *L) {
    if (!Game::Lua::IsString(L, 1))
        Game::Lua::Error(L, "Usage: IsBindingForGamePad(\"KEY\")");
    Game::Lua::PushBool(L, IsPadKey(Game::Lua::ToString(L, 1)));
    return 1;
}

const Game::Doc::Field kKeyArg[] = {Game::Doc::Req("key", "cstring", "A key or chord, such as SHIFT-PAD1.")};
const Game::Doc::Field kIsPadRets[] = {Game::Doc::Req("isGamePad", "bool")};
const Game::Doc::Function kIsBindingForGamePad{"Returns whether a key or chord is a gamepad button.", kKeyArg,
                                               kIsPadRets, "BindingsGlobals"};

// 1.12's SetBinding refuses PAD names (FUN_IS_VALID_BINDING_KEY), and its
// bindings-cache.wtf loader drops what it refuses. 3.80.2's validator
// (0x14124D200) also takes a PAD name after the modifiers.
using IsValidKey_t = int(__fastcall *)(const char *key);
IsValidKey_t g_origIsValidKey = nullptr;

int __fastcall IsValidKey_h(const char *key) {
    if (g_origIsValidKey(key) != 0)
        return 1;
    if (key == nullptr)
        return 0;
    for (bool matched = true; matched;) {
        matched = false;
        for (const char *p : {"SHIFT-", "CTRL-", "ALT-"}) {
            const size_t n = std::strlen(p);
            if (std::strncmp(key, p, n) == 0) {
                key += n;
                matched = true;
            }
        }
    }
    return Names::BindingToIndex(key) != Names::kNone ? 1 : 0;
}

const Game::HookAutoRegister _validKeyHook{Offsets::FUN_IS_VALID_BINDING_KEY,
                                           reinterpret_cast<void *>(&IsValidKey_h),
                                           reinterpret_cast<void **>(&g_origIsValidKey)};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
    Game::Lua::RegisterGlobalFunction("IsBindingForGamePad", &Script_IsBindingForGamePad, &kIsBindingForGamePad);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Dispatch() {
    for (bool &u : g_unclaimed)
        u = false;
    g_pendingModifiers = g_releasedModifiers = 0;
    const uint32_t id = ActiveID();
    const Device::Slot *slot = Device::SlotByID(id);
    if (!Started() || slot == nullptr || !slot->connected) {
        // 3.80.2 stops here and leaves held buttons held; a device that is gone
        // can never send their ups, so they are released.
        ReleaseAll();
        return;
    }
    const State::Mapped &m = State::MappedOf(id);

    // Touchpad clicks and taps click directly, without cursor mode.
    if (Device::Touch().cursorEnable) {
        const uint32_t changed = m.touchButton ^ m.prevTouchButton;
        if ((changed & 1) != 0)
            Cursor::Click(kLeftClick, (m.touchButton & 1) != 0);
        if ((changed & 4) != 0)
            Cursor::Click(kRightClick, (m.touchButton & 4) != 0);
    }

    const uint32_t ups = g_tapUps;
    g_tapUps = 0;
    for (uint8_t b = 0; b < Names::kButtonCount; ++b)
        if ((ups >> b) & 1u)
            SendButton(b, false);
    const uint32_t taps = g_tapDowns;
    g_tapDowns = 0;
    for (uint8_t b = 0; b < Names::kButtonCount; ++b)
        if ((taps >> b) & 1u) {
            SendButton(b, true);
            g_tapUps |= 1u << b;
        }

    const uint32_t changed = m.pressed | m.released;
    for (uint8_t b = 0; b < Names::kButtonCount; ++b) {
        const uint32_t bit = 1u << b;
        if ((changed & bit) == 0)
            continue;
        const bool prevDown = (m.prevButtons & bit) != 0;
        const bool curDown = (m.buttons & bit) != 0;
        const uint32_t first = prevDown ? m.released : m.pressed;
        const uint32_t second = prevDown ? m.pressed : m.released;
        const bool firstDown = !prevDown;
        if ((first & bit) == 0)
            continue;
        Emit(b, firstDown);
        if ((second & bit) != 0) {
            Emit(b, !firstDown);
            if (curDown != prevDown)
                Emit(b, firstDown);
        }
    }

    for (int s = 0; s < Names::kStickCount; ++s) {
        const State::StickValue &v = m.sticks[s];
        if (v.len > 0.0f || g_lastSentLen[s] > 0.0f) {
            SendStick(s, v);
            g_lastSentLen[s] = v.len;
        }
    }
}

void Reset() { ReleaseAll(); }

bool StickUnclaimed(int stick) { return stick >= 0 && stick < Names::kStickCount && g_unclaimed[stick]; }

bool InPadBinding() { return g_inPadBinding; }

void AssignClicks(bool on) {
    if (on == g_clicksAssigned)
        return;
    if (on && g_cvLeftClick != nullptr && g_cvRightClick != nullptr) {
        g_clicksAssigned = true;
        Assign(kLeftClick, Names::BindingToIndex(CVar::Factory::GetString(g_cvLeftClick)));
        Assign(kRightClick, Names::BindingToIndex(CVar::Factory::GetString(g_cvRightClick)));
    } else {
        g_clicksAssigned = false;
        Assign(kLeftClick, Names::kNone);
        Assign(kRightClick, Names::kNone);
    }
}

} // namespace GamePad::Buttons
