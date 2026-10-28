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

// Mouse clicks and mouse motion as separate inputs:
// `SetMouseClickEnabled`, `SetMouseMotionEnabled`, `SetPropagateMouseClicks`
// and their getters, as retail has them (probed, docs/notes/mouse-motion-probe.md).
//
// Retail runs two walks over the frames under the cursor. Motion goes to the
// top motion-enabled frame (Frame::MotionFocus, which skips frames whose
// motion is off). A press goes to the top CLICK-enabled frame, whatever holds
// the motion: a click on a motion-only frame reaches the click-enabled frame
// under it, and a click-only frame takes the press without ever taking the
// motion. A frame that propagates clicks passes the press on to the next
// click-enabled frame under the cursor; each gets the press and then the
// release, top-first.
//
// 1.12 has one mouse bit (OFF_FRAME_ENABLED_EVENTS bit 2), which puts the
// frame in the mouse lists. It stays "click OR motion"; a frame with one half
// off is in a set of its own. EnableMouse sets both halves, so it clears
// that. IsMouseEnabled is both halves, still answered as the engine answers
// it: 1 or nil, which addons compare with other 1/nil values.
//
// The press handler targets the pressed frame, else the focus (+0x7C); the
// focus read is replaced with the click walk. While no frame has a half off
// or propagates clicks the walk is the focus read, so nothing changes for a
// UI that does not use them. Otherwise the walk reuses what the motion pass
// already decided: a frame in the motion focus is hit; a motion-enabled frame
// above the first focus frame was probed by the pass and missed; any other
// click-enabled frame gets the engine's hit test. The press and release
// dispatches run each extra target's button slot after the engine's.

#include "frame/MouseInput.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/Destruction.h"
#include "frame/MotionFocus.h"
#include "xml/FrameLoad.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <unordered_set>
#include <vector>

namespace Frame::MouseInput {

namespace {

using EnableEvent_t = void(__thiscall *)(void *frame, int kind, int priority);
using DisableEvent_t = void(__thiscall *)(void *frame, int kind);
using Button_t = void(__thiscall *)(void *frame, const void *event);
using HitTest_t = int(__thiscall *)(void *frame, const float *point);
using NDCToDDC_t = void(__fastcall *)(float *outX, float *outY, float x, float y);

// Frames with their mouse bit on and that half off.
std::unordered_set<void *> g_clickOff;
std::unordered_set<void *> g_motionOff;
std::unordered_set<void *> g_propagatesClicks;

struct Press {
    void *primary = nullptr;
    std::vector<void *> extras;
};
Press g_pending; // the click walk's answer for the press being handled
Press g_pressed; // the press whose release is outstanding

uint8_t *Context() { return Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR); }

void Repick() {
    if (uint8_t *ctx = Context())
        Game::Ref<int>(ctx, Offsets::OFF_UI_CONTEXT_REPICK) = 1;
}

bool MouseBit(const void *frame) {
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_ENABLED_EVENTS) >> Offsets::SIMPLE_EVENT_MOUSE & 1) != 0;
}

bool ClickEnabled(void *frame) { return MouseBit(frame) && g_clickOff.count(frame) == 0; }

bool PropagatesClicks(void *frame) { return g_propagatesClicks.count(frame) != 0; }

void SetHalves(void *frame, bool click, bool motion) {
    const bool wasMotion = MotionEnabled(frame);
    const bool bit = click || motion;
    if (bit != MouseBit(frame)) {
        if (bit)
            reinterpret_cast<EnableEvent_t>(Offsets::FUN_FRAME_ENABLE_EVENT)(frame, Offsets::SIMPLE_EVENT_MOUSE, -1);
        else
            reinterpret_cast<DisableEvent_t>(Offsets::FUN_FRAME_DISABLE_EVENT)(frame, Offsets::SIMPLE_EVENT_MOUSE);
    }
    if (bit && !click)
        g_clickOff.insert(frame);
    else
        g_clickOff.erase(frame);
    if (bit && !motion)
        g_motionOff.insert(frame);
    else
        g_motionOff.erase(frame);
    if (MotionEnabled(frame) != wasMotion)
        Repick();
}

// The click walk: the top click-enabled frame under the cursor, then the
// next ones while the last one taken propagates clicks.
void ClickTargets(uint8_t *ctx, std::vector<void *> &out) {
    void *const primary = Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS);
    const float *mouse = Game::Ptr<float>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    float point[2] = {0.0f, 0.0f};
    reinterpret_cast<NDCToDDC_t>(Offsets::FUN_COORD_NDC_TO_DDC)(&point[0], &point[1], mouse[0], mouse[1]);
    const auto hitTest = reinterpret_cast<HitTest_t>(Offsets::FUN_FRAME_HIT_TEST);

    bool pastPrimary = primary == nullptr;
    for (int strata = Offsets::FRAME_STRATA_COUNT - 1; strata >= 0; --strata) {
        const uint8_t *queue = ctx + Offsets::OFF_UI_CONTEXT_EVENT_QUEUES +
                               (strata + Offsets::FRAME_STRATA_COUNT * Offsets::SIMPLE_EVENT_MOUSE) *
                                   Offsets::EVENT_QUEUE_STRIDE;
        const uint32_t count = Game::Read<uint32_t>(queue, Offsets::OFF_EVENT_QUEUE_COUNT);
        auto *const *nodes = Game::Read<void *const *>(queue, Offsets::OFF_EVENT_QUEUE_DATA);
        for (uint32_t i = 0; i < count; ++i) {
            void *frame = Game::Read<void *>(nodes[i], 0);
            if (frame == primary)
                pastPrimary = true;
            if (!ClickEnabled(frame))
                continue;
            bool hit;
            if (Frame::MotionFocus::IsFocus(frame))
                hit = true;
            else if (MotionEnabled(frame) && !pastPrimary)
                hit = false;
            else
                hit = hitTest(frame, point) != 0;
            if (!hit)
                continue;
            out.push_back(frame);
            if (!PropagatesClicks(frame))
                return;
        }
    }
}

void *__stdcall PressTarget(uint8_t *ctx) {
    g_pending = Press{};
    if (g_clickOff.empty() && g_motionOff.empty() && g_propagatesClicks.empty())
        return Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS);
    std::vector<void *> targets;
    ClickTargets(ctx, targets);
    if (targets.empty())
        return nullptr;
    g_pending.primary = targets.front();
    g_pending.extras.assign(targets.begin() + 1, targets.end());
    return g_pending.primary;
}

void RunButton(void *frame, uintptr_t slot, const void *event) {
    const void *vtable = Game::Read<const void *>(frame, 0);
    Game::Read<Button_t>(vtable, slot)(frame, event);
}

// Runs a list's button slot; entries a script nulls out are skipped.
void RunExtras(std::vector<void *> extras, uintptr_t slot, const void *event) {
    for (void *f : extras) {
        if (f != nullptr && std::find(g_pressed.extras.begin(), g_pressed.extras.end(), f) != g_pressed.extras.end())
            RunButton(f, slot, event);
    }
}

void __stdcall PressDispatch(uint8_t * /*ctx*/, void *frame, const void *event) {
    if (frame == g_pending.primary)
        g_pressed = g_pending;
    else if (frame != g_pressed.primary)
        g_pressed = Press{frame, {}};
    g_pending = Press{};
    RunButton(frame, Offsets::VTBL_FRAME_ON_MOUSE_DOWN, event);
    RunExtras(g_pressed.extras, Offsets::VTBL_FRAME_ON_MOUSE_DOWN, event);
}

void __stdcall ReleaseDispatch(uint8_t * /*ctx*/, void *frame, const void *event) {
    RunButton(frame, Offsets::VTBL_FRAME_ON_MOUSE_UP, event);
    if (frame == g_pressed.primary)
        RunExtras(g_pressed.extras, Offsets::VTBL_FRAME_ON_MOUSE_UP, event);
    if (Game::Read<int>(event, Offsets::OFF_MOUSE_EVENT_BUTTONS) == 0)
        g_pressed = Press{};
}

// Replaces `mov esi,[ebx+0x7C]; test esi,esi`: context in ebx; the `jz`
// after it reads the flags this leaves.
__declspec(naked) void PressTargetThunk() {
    __asm {
        push ebx
        call PressTarget
        mov esi, eax
        test esi, esi
        ret
    }
}

// Replaces the press's button-down call: frame in esi, event in eax,
// context in ebx.
__declspec(naked) void PressDispatchThunk() {
    __asm {
        push eax
        push esi
        push ebx
        call PressDispatch
        ret
    }
}

// Replaces the release's button-up call: frame in ebx, context in esi, the
// event at ebp-0x3C.
__declspec(naked) void ReleaseDispatchThunk() {
    __asm {
        lea eax, [ebp - 3Ch]
        push eax
        push ebx
        push esi
        call ReleaseDispatch
        ret
    }
}

const uint8_t kPressTarget[] = {0x8B, 0x73, 0x7C, 0x85, 0xF6};
const uint8_t kPressDispatch[] = {0x8B, 0x16, 0x50, 0x8B, 0xCE, 0xFF, 0x52, 0x68};
const uint8_t kReleaseDispatch[] = {0x8B, 0x03, 0x8D, 0x4D, 0xC4, 0x51, 0x8B, 0xCB, 0xFF, 0x50, 0x6C};

void Forget(void *frame) {
    for (Press *p : {&g_pending, &g_pressed})
        std::replace(p->extras.begin(), p->extras.end(), frame, static_cast<void *>(nullptr));
}

void OnFrameDestroyed(void *frame) {
    g_clickOff.erase(frame);
    g_motionOff.erase(frame);
    g_propagatesClicks.erase(frame);
    Forget(frame);
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

// `enableMouseClicks="true"` and `enableMouseMotion="true"` add their half;
// `enableMouse="true"`, which the engine has applied by now, already holds
// both. `propagateMouseInput="Clicks"` or `"Both"` propagates clicks.
void OnFrameNode(void *object, const void *node, void * /*status*/) {
    const char *clicks = Xml::Attr(node, "enableMouseClicks");
    const char *motion = Xml::Attr(node, "enableMouseMotion");
    const bool addClick = clicks != nullptr && Game::StringToFlag(clicks);
    const bool addMotion = motion != nullptr && Game::StringToFlag(motion);
    if (addClick || addMotion)
        SetHalves(object, addClick || ClickEnabled(object), addMotion || MotionEnabled(object));
    const char *propagate = Xml::Attr(node, "propagateMouseInput");
    if (Ascii::EqualCI(propagate, "Clicks") || Ascii::EqualCI(propagate, "Both"))
        g_propagatesClicks.insert(object);
}

const Xml::FrameLoad::AutoSubscribe _xmlSub{&OnFrameNode};

int __fastcall Script_EnableMouse(void *L) {
    reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_FRAME_ENABLEMOUSE)(L);
    void *frame = Game::Lua::ResolveFrame(L, 1, false);
    if (frame != nullptr && (g_clickOff.erase(frame) | g_motionOff.erase(frame)) != 0)
        Repick();
    return 0;
}

int __fastcall Script_IsMouseEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L, 1, false);
    if (frame != nullptr && (g_clickOff.count(frame) != 0 || g_motionOff.count(frame) != 0)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_FRAME_ISMOUSEENABLED)(L);
}

int __fastcall Script_SetMouseClickEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    SetHalves(frame, Game::Lua::ToFlag(L, 2), MotionEnabled(frame));
    return 0;
}

int __fastcall Script_SetMouseMotionEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    SetHalves(frame, ClickEnabled(frame), Game::Lua::ToFlag(L, 2));
    return 0;
}

int __fastcall Script_IsMouseClickEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, ClickEnabled(frame));
    return 1;
}

int __fastcall Script_IsMouseMotionEnabled(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, MotionEnabled(frame));
    return 1;
}

int __fastcall Script_SetPropagateMouseClicks(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (Game::Lua::ToFlag(L, 2))
        g_propagatesClicks.insert(frame);
    else
        g_propagatesClicks.erase(frame);
    return 0;
}

int __fastcall Script_CanPropagateMouseClicks(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, PropagatesClicks(frame));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"EnableMouse", &Script_EnableMouse},
    {"IsMouseEnabled", &Script_IsMouseEnabled},
    {"SetMouseClickEnabled", &Script_SetMouseClickEnabled},
    {"SetMouseMotionEnabled", &Script_SetMouseMotionEnabled},
    {"IsMouseClickEnabled", &Script_IsMouseClickEnabled},
    {"IsMouseMotionEnabled", &Script_IsMouseMotionEnabled},
    {"SetPropagateMouseClicks", &Script_SetPropagateMouseClicks},
    {"CanPropagateMouseClicks", &Script_CanPropagateMouseClicks},
};

const Game::Doc::Field kClickArgs[] = {
    Game::Doc::Req("enabled", "bool", "When true, the frame takes mouse clicks."),
};
const Game::Doc::Field kMotionArgs[] = {
    Game::Doc::Req("enabled", "bool", "When true, the frame takes mouse motion: OnEnter and OnLeave."),
};
const Game::Doc::Field kClickRets[] = {
    Game::Doc::Req("enabled", "bool", "True when the frame takes mouse clicks."),
};
const Game::Doc::Field kMotionRets[] = {
    Game::Doc::Req("enabled", "bool", "True when the frame takes mouse motion."),
};
const Game::Doc::Field kPropagateArgs[] = {
    Game::Doc::Req("propagate", "bool",
                   "When true, a click on the frame also goes to the next frame under the cursor."),
};
const Game::Doc::Field kPropagateRets[] = {
    Game::Doc::Req("propagate", "bool", "True when the frame passes mouse clicks on."),
};
const Game::Doc::Function kSetClick{"Sets whether the frame takes mouse clicks.", kClickArgs, {}};
const Game::Doc::Function kSetMotion{"Sets whether the frame takes mouse motion.", kMotionArgs, {}};
const Game::Doc::Function kIsClick{"Returns whether the frame takes mouse clicks.", {}, kClickRets};
const Game::Doc::Function kIsMotion{"Returns whether the frame takes mouse motion.", {}, kMotionRets};
const Game::Doc::Function kSetPropagate{
    "Sets whether a click on the frame also goes to the next frame under the cursor.", kPropagateArgs, {}};
const Game::Doc::Function kCanPropagate{"Returns whether the frame passes mouse clicks on.", {}, kPropagateRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetMouseClickEnabled", &kSetClick},
    {"SetMouseMotionEnabled", &kSetMotion},
    {"IsMouseClickEnabled", &kIsClick},
    {"IsMouseMotionEnabled", &kIsMotion},
    {"SetPropagateMouseClicks", &kSetPropagate},
    {"CanPropagateMouseClicks", &kCanPropagate},
};

void RegisterLuaFunctions() {
    Common::ReplaceWithCall(Offsets::ADDR_UI_PRESS_TARGET, kPressTarget, sizeof kPressTarget,
                            reinterpret_cast<const void *>(&PressTargetThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_PRESS_DISPATCH, kPressDispatch, sizeof kPressDispatch,
                            reinterpret_cast<const void *>(&PressDispatchThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_RELEASE_DISPATCH, kReleaseDispatch, sizeof kReleaseDispatch,
                            reinterpret_cast<const void *>(&ReleaseDispatchThunk));
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
                                    static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
                                    static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool MotionEnabled(void *frame) { return MouseBit(frame) && g_motionOff.count(frame) == 0; }

void OnInputRemoved(void *frame) {
    g_pressed.extras.erase(std::remove(g_pressed.extras.begin(), g_pressed.extras.end(), frame),
                           g_pressed.extras.end());
}

} // namespace Frame::MouseInput
