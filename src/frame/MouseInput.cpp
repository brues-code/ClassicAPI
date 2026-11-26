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
//
// `SetPassThroughButtons(...)` names buttons the frame lets through: the walk
// skips it for a press of one of them, so the frame gets nothing for that
// press and the next click-enabled frame under the cursor takes it (retail:
// the frame still got its other buttons, the one below got the passed one,
// and an unknown name raised no error). The world frame is in the same mouse
// lists, so a press with no frame below reaches the world. Hover is
// untouched. A press while another button is held goes to the frame already
// pressed, as the engine routes it; the walk does not run for it.
//
// Each frame a propagated press reached can start its own drag, as 3.80.2
// drag-tests every frame in a press list on a mouse move (FUN_14058B000):
// the hover pass probes them as the engine probes its pressed frame
// (benilla-re ui/mouse.md "Starting a drag"), and each one's release slot
// ends its drag with OnDragStop and no OnMouseUp, as the base release does.
//
// Turning a half off while the mouse bit stays on does what 3.80.2 does when
// it unregisters that half (FUN_14055F5A0): clicks off takes the frame out of
// the press, so it gets no release; motion off takes it out of the motion
// focus at once, with OnLeave(false). The setters' rules are 3.80.2's:
// EnableMouseMotion and SetMouse*Enabled read a missing argument as false,
// SetPropagateMouseClicks requires its argument, SetPassThroughButtons
// raises the usage error for an argument that is not a string.
//
// Textures and font strings take clicks too (Frame::RegionInput). The click
// walk merges them with the frames in draw order (RegionInput::Above), so the
// press target, and each one a propagated press reaches, can be a region,
// which runs its OnMouseDown (self, button) and, on the release, OnMouseUp
// (self, button, upInside), upInside true when the cursor is still over it.
// 1.12's press handler knows only frames, so a press whose target is a
// region is run here: the target step answers no frame and sets the
// handler's [ebp-8], which makes it report the press taken instead of passed
// on. While that press is held, both handlers' host-button gates read it as
// a pressed frame (the host's callback does not run), a second button goes
// to it, and the release's no-frame branch runs its OnMouseUp. A region that
// is hidden, has its clicks turned off or is destroyed leaves the press and
// gets no OnMouseUp.

#include "frame/MouseInput.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "frame/Layout.h"
#include "baselib/Ascii.h"
#include "frame/ClickEvents.h"
#include "frame/Destruction.h"
#include "frame/MotionFocus.h"
#include "frame/RegionInput.h"
#include "frame/RegionScripts.h"
#include "xml/FrameLoad.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Frame::MouseInput {

namespace {

using EnableEvent_t = void(__thiscall *)(void *frame, int kind, int priority);
using DisableEvent_t = void(__thiscall *)(void *frame, int kind);
using Button_t = void(__thiscall *)(void *frame, const void *event);
using HitTest_t = int(__thiscall *)(void *frame, const float *point);

// Frames with their mouse bit on and that half off.
std::unordered_set<void *> g_clickOff;
std::unordered_set<void *> g_motionOff;
std::unordered_set<void *> g_propagatesClicks;
// `propagateMouseInputMask="Clicks"`: the frame never propagates clicks,
// whatever SetPropagateMouseClicks says (3.80.2 keeps it at +0x123 bit 0x20
// and reads the flag as +0x122 && !+0x123).
std::unordered_set<void *> g_clicksSuppressed;
std::unordered_map<void *, uint32_t> g_passThrough; // button bits the frame lets through

struct Press {
    void *primary = nullptr;
    std::vector<void *> extras;
};
Press g_pending; // the click walk's answer for the press being handled
Press g_pressed; // the press whose release is outstanding

// 1 while the press outstanding is one a region took (g_pressed.primary is
// that region, or null once it left the press): no frame is pressed, but the
// press handlers treat it as held. An int, read by the gate thunks.
int g_regionPress = 0;
// Set by PressTarget when it ran the press itself; the thunk stores it in the
// handler's [ebp-8], so the null target reports the press taken.
int g_regionTook = 0;
constexpr uintptr_t kReleasePassedOn = Offsets::ADDR_UI_RELEASE_PASSED_ON;

uint8_t *Context() { return Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR); }

void Repick() {
    if (uint8_t *ctx = Context())
        Game::Ref<int>(ctx, Offsets::OFF_UI_CONTEXT_REPICK) = 1;
}

bool MouseBit(const void *frame) {
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_ENABLED_EVENTS) >> Offsets::SIMPLE_EVENT_MOUSE & 1) != 0;
}

bool ClickEnabled(void *frame) { return MouseBit(frame) && g_clickOff.count(frame) == 0; }

bool PropagatesClicks(void *frame) {
    return g_propagatesClicks.count(frame) != 0 && g_clicksSuppressed.count(frame) == 0;
}

uint32_t PassThroughMask(void *frame) {
    const auto it = g_passThrough.find(frame);
    return it != g_passThrough.end() ? it->second : 0;
}

void SetPassThrough(void *frame, uint32_t mask) {
    if (mask != 0)
        g_passThrough[frame] = mask;
    else
        g_passThrough.erase(frame);
}

// "LeftButton, RightButton": the names in a comma-separated list.
uint32_t MaskForButtonList(const char *list) {
    uint32_t mask = 0;
    char name[32];
    while (*list != '\0') {
        while (*list == ',' || *list == ' ' || *list == '\t')
            ++list;
        size_t n = 0;
        while (list[n] != '\0' && list[n] != ',' && list[n] != ' ' && list[n] != '\t')
            ++n;
        if (n > 0 && n < sizeof name) {
            std::memcpy(name, list, n);
            name[n] = '\0';
            mask |= Frame::ClickEvents::MaskForButtonName(name);
        }
        list += n;
    }
    return mask;
}

void Forget(void *frame);

// Takes the frame out of the press in progress: it gets no release.
void CancelPress(void *frame) {
    Forget(frame);
    if (uint8_t *ctx = Context()) {
        void *&pressed = Game::Ref<void *>(ctx, Offsets::OFF_UI_CONTEXT_PRESSED_FRAME);
        if (pressed == frame)
            pressed = nullptr;
    }
}

void SetHalves(void *frame, bool click, bool motion) {
    const bool wasMotion = MotionEnabled(frame);
    const bool wasClick = ClickEnabled(frame);
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
    // With the bit off the engine's own removal has done both.
    if (bit && wasClick && !ClickEnabled(frame))
        CancelPress(frame);
    if (bit && wasMotion && !MotionEnabled(frame))
        Frame::MotionFocus::Drop(frame);
    else if (MotionEnabled(frame) != wasMotion)
        Repick();
}

// The click walk: the top click-enabled frame under the cursor that does not
// let `button` through, then the next ones while the last one taken
// propagates clicks.
//
// The regions under the cursor that take the button join in draw order: each
// region that draws over the next frame hit is taken before it.
void ClickTargets(uint8_t *ctx, uint32_t button, std::vector<void *> &out) {
    void *const primary = Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS);
    float point[2] = {0.0f, 0.0f};
    Frame::RegionInput::Cursor(ctx, point);
    const auto hitTest = reinterpret_cast<HitTest_t>(Offsets::FUN_FRAME_HIT_TEST);
    std::vector<void *> regions;
    if (Frame::RegionInput::Any())
        Frame::RegionInput::Candidates(Frame::RegionInput::CLICK, point, button, regions);
    size_t next = 0;
    // Takes the regions over `frame` (all that are left for null); true when
    // one taken stops the walk.
    const auto regionsOver = [&](const void *frame) {
        while (next < regions.size() && (frame == nullptr || Frame::RegionInput::Above(regions[next], frame))) {
            void *region = regions[next++];
            out.push_back(region);
            if (!Frame::RegionInput::PropagatesClicks(region))
                return true;
        }
        return false;
    };

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
            if (!ClickEnabled(frame) || (PassThroughMask(frame) & button) != 0)
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
            if (regionsOver(frame))
                return;
            out.push_back(frame);
            if (!PropagatesClicks(frame))
                return;
        }
    }
    regionsOver(nullptr);
}

bool IsRegion(const void *object) { return Frame::RegionScripts::IsRegion(object); }

const char *ButtonName(const void *event) {
    return Frame::ClickEvents::ButtonNameForMask(Game::Read<uint32_t>(event, Offsets::OFF_UI_MOUSE_EVENT_BUTTON));
}

void RegionDown(void *region, const void *event) {
    const char *name = ButtonName(event);
    Frame::RegionScripts::Run(region, Frame::RegionScripts::ON_MOUSE_DOWN, [name](void *L) {
        Game::Lua::PushString(L, name);
        return 1;
    });
}

void RegionUp(void *region, const void *event) {
    const char *name = ButtonName(event);
    bool inside = false;
    if (uint8_t *ctx = Context()) {
        float point[2];
        Frame::RegionInput::Cursor(ctx, point);
        inside = Frame::RegionInput::Under(region, point);
    }
    Frame::RegionScripts::Run(region, Frame::RegionScripts::ON_MOUSE_UP, [name, inside](void *L) {
        Game::Lua::PushString(L, name);
        Game::Lua::PushBool(L, inside);
        return 2;
    });
}

// A frame's button slot, or a region's OnMouseDown / OnMouseUp.
void RunButton(void *object, uintptr_t slot, const void *event) {
    if (IsRegion(object)) {
        if (slot == Offsets::VTBL_FRAME_ON_MOUSE_DOWN)
            RegionDown(object, event);
        else
            RegionUp(object, event);
        return;
    }
    const void *vtable = Game::Read<const void *>(object, 0);
    Game::Read<Button_t>(vtable, slot)(object, event);
}

// Runs a list's button slot; entries a script nulls out are skipped.
void RunExtras(std::vector<void *> extras, uintptr_t slot, const void *event) {
    for (void *f : extras) {
        if (f != nullptr && std::find(g_pressed.extras.begin(), g_pressed.extras.end(), f) != g_pressed.extras.end())
            RunButton(f, slot, event);
    }
}

// GLOBAL_REGION_MOUSE_DOWN / GLOBAL_REGION_MOUSE_UP(region, button)
// (_forever_ only; no 3.80.2 counterpart): the frame or region a press or
// release goes to, fired before its script runs, as 3.80.2 fires its global
// mouse hook before the press list. The world frame and no target at all
// are nil: the world was clicked.
const Game::Doc::Field kRegionMousePayload[] = {
    Game::Doc::Opt("region", "ScriptRegion", nullptr, "Nil when the click went to the world or to nothing."),
    Game::Doc::Req("button", "cstring"),
};
const Game::Doc::Event kRegionMouseDownDoc{
    "UIParent", "Fires when a mouse button is pressed, with the frame or region the press goes to.",
    kRegionMousePayload};
const Game::Doc::Event kRegionMouseUpDoc{
    "UIParent", "Fires when a mouse button is released, with the frame or region the release goes to.",
    kRegionMousePayload};
const Event::Custom::AutoReserve _regionMouseDown{"GLOBAL_REGION_MOUSE_DOWN", &kRegionMouseDownDoc};
const Event::Custom::AutoReserve _regionMouseUp{"GLOBAL_REGION_MOUSE_UP", &kRegionMouseUpDoc};

void FireRegionMouse(const Event::Custom::AutoReserve &reserve, void *object, const void *event) {
    const int slot = reserve.Slot();
    void *L = Game::Lua::State();
    if (slot < 0 || L == nullptr)
        return;
    if (object == Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR))
        object = nullptr;
    const int top = Game::Lua::GetTop(L);
    if (object != nullptr)
        Game::Lua::PushObject(L, object);
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushString(L, ButtonName(event));
    Event::Custom::FireValues(slot, L, top + 1, 2);
    Game::Lua::SetTop(L, top);
}

// Runs a press whose target is a region: down on the target, then on each
// object the press propagated to.
void RunRegionPress(const void *event) {
    FireRegionMouse(_regionMouseDown, g_pressed.primary, event);
    if (g_pressed.primary != nullptr)
        RegionDown(g_pressed.primary, event);
    RunExtras(g_pressed.extras, Offsets::VTBL_FRAME_ON_MOUSE_DOWN, event);
}

void *__stdcall PressTarget(uint8_t *ctx, const void *event) {
    g_pending = Press{};
    g_regionTook = 0;
    if (g_regionPress != 0) { // another button while a region's press is held
        RunRegionPress(event);
        g_regionTook = 1;
        return nullptr;
    }
    if (g_clickOff.empty() && g_motionOff.empty() && g_propagatesClicks.empty() && g_passThrough.empty() &&
        !Frame::RegionInput::Any()) {
        void *focus = Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS);
        if (focus == nullptr)
            FireRegionMouse(_regionMouseDown, nullptr, event);
        return focus;
    }
    std::vector<void *> targets;
    ClickTargets(ctx, Game::Read<uint32_t>(event, Offsets::OFF_UI_MOUSE_EVENT_BUTTON), targets);
    if (targets.empty()) {
        FireRegionMouse(_regionMouseDown, nullptr, event);
        return nullptr;
    }
    if (IsRegion(targets.front())) {
        g_pressed = Press{targets.front(), std::vector<void *>(targets.begin() + 1, targets.end())};
        g_regionPress = 1;
        RunRegionPress(event);
        g_regionTook = 1;
        return nullptr;
    }
    g_pending.primary = targets.front();
    g_pending.extras.assign(targets.begin() + 1, targets.end());
    return g_pending.primary;
}

// The release with no pressed frame: a region's press, if one is held.
// Returns what the handler's [ebp-8] takes, 1 for taken.
int __stdcall ReleaseRegion(const void *event) {
    if (g_regionPress == 0) {
        FireRegionMouse(_regionMouseUp, nullptr, event);
        return 0;
    }
    FireRegionMouse(_regionMouseUp, g_pressed.primary, event);
    if (g_pressed.primary != nullptr)
        RegionUp(g_pressed.primary, event);
    RunExtras(g_pressed.extras, Offsets::VTBL_FRAME_ON_MOUSE_UP, event);
    if (Game::Read<int>(event, Offsets::OFF_MOUSE_EVENT_BUTTONS) == 0) {
        g_pressed = Press{};
        g_regionPress = 0;
    }
    return 1;
}

void __stdcall PressDispatch(uint8_t * /*ctx*/, void *frame, const void *event) {
    if (frame == g_pending.primary)
        g_pressed = g_pending;
    else if (frame != g_pressed.primary)
        g_pressed = Press{frame, {}};
    g_pending = Press{};
    FireRegionMouse(_regionMouseDown, frame, event);
    RunButton(frame, Offsets::VTBL_FRAME_ON_MOUSE_DOWN, event);
    RunExtras(g_pressed.extras, Offsets::VTBL_FRAME_ON_MOUSE_DOWN, event);
}

void __stdcall ReleaseDispatch(uint8_t * /*ctx*/, void *frame, const void *event) {
    FireRegionMouse(_regionMouseUp, frame, event);
    RunButton(frame, Offsets::VTBL_FRAME_ON_MOUSE_UP, event);
    if (frame == g_pressed.primary)
        RunExtras(g_pressed.extras, Offsets::VTBL_FRAME_ON_MOUSE_UP, event);
    if (Game::Read<int>(event, Offsets::OFF_MOUSE_EVENT_BUTTONS) == 0)
        g_pressed = Press{};
}

// Replaces `mov esi,[ebx+0x7C]; test esi,esi`: context in ebx, the event
// copy at ebp-0x40 (the press's own frame); the `jz` after it reads the
// flags this leaves.
// A press PressTarget ran itself sets [ebp-8], so the `jz` to the no-target
// return reports it taken.
__declspec(naked) void PressTargetThunk() {
    __asm {
        lea eax, [ebp - 40h]
        push eax
        push ebx
        call PressTarget
        mov esi, eax
        mov ecx, g_regionTook
        or dword ptr [ebp - 8], ecx
        test esi, esi
        ret
    }
}

// Replaces the press's `mov ecx,[ebx+0x80]` before the host gate's `test
// ecx,ecx; jne`: a held region press reads as a pressed frame.
__declspec(naked) void PressHostGateThunk() {
    __asm {
        mov ecx, dword ptr [ebx + 80h]
        or ecx, g_regionPress
        ret
    }
}

// The release's twin, context in esi.
__declspec(naked) void ReleaseHostGateThunk() {
    __asm {
        mov ecx, dword ptr [esi + 80h]
        or ecx, g_regionPress
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

// Replaces the release's null test and button-up call: frame in ebx (null
// when none is pressed), context in esi, the event at ebp-0x3C. With a frame
// it runs the dispatch and returns after the call; without one it runs a held
// region's release, stores the answer in [ebp-8] and goes on to the
// handler's no-frame return.
__declspec(naked) void ReleaseDispatchThunk() {
    __asm {
        test ebx, ebx
        jz none
        lea eax, [ebp - 3Ch]
        push eax
        push ebx
        push esi
        call ReleaseDispatch
        ret
    none:
        lea eax, [ebp - 3Ch]
        push eax
        call ReleaseRegion
        mov dword ptr [ebp - 8], eax
        add esp, 4
        push kReleasePassedOn
        ret
    }
}

const uint8_t kPressTarget[] = {0x8B, 0x73, 0x7C, 0x85, 0xF6};
const uint8_t kPressDispatch[] = {0x8B, 0x16, 0x50, 0x8B, 0xCE, 0xFF, 0x52, 0x68};
const uint8_t kReleaseDispatch[] = {0x85, 0xDB, 0x74, 0x31, 0x8B, 0x03, 0x8D, 0x4D,
                                    0xC4, 0x51, 0x8B, 0xCB, 0xFF, 0x50, 0x6C};
const uint8_t kPressHostGate[] = {0x8B, 0x8B, 0x80, 0x00, 0x00, 0x00};
const uint8_t kReleaseHostGate[] = {0x8B, 0x8E, 0x80, 0x00, 0x00, 0x00};

void Forget(void *frame) {
    for (Press *p : {&g_pending, &g_pressed}) {
        std::replace(p->extras.begin(), p->extras.end(), frame, static_cast<void *>(nullptr));
        if (p->primary == frame && IsRegion(frame))
            p->primary = nullptr;
    }
}

void OnFrameDestroyed(void *frame) {
    g_clickOff.erase(frame);
    g_motionOff.erase(frame);
    g_propagatesClicks.erase(frame);
    g_clicksSuppressed.erase(frame);
    g_passThrough.erase(frame);
    Forget(frame);
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

// `enableMouseClicks="true"` and `enableMouseMotion="true"` add their half;
// `enableMouse="true"`, which the engine has applied by now, already holds
// both. `propagateMouseInput="Clicks"` or `"Both"` propagates clicks;
// `propagateMouseInputMask` with the same values keeps them from
// propagating. `retainClickThroughOverride` is not read: it keeps a frame in
// the click list while an override frame holds the mouse, and 1.12 has no
// override frames.
// `passThroughButtons="LeftButton, RightButton"` lets those buttons through.
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
    const char *mask = Xml::Attr(node, "propagateMouseInputMask");
    if (Ascii::EqualCI(mask, "Clicks") || Ascii::EqualCI(mask, "Both"))
        g_clicksSuppressed.insert(object);
    if (const char *buttons = Xml::Attr(node, "passThroughButtons"))
        SetPassThrough(object, MaskForButtonList(buttons));
}

const Xml::FrameLoad::AutoSubscribe _xmlSub{&OnFrameNode};

int __fastcall Script_EnableMouse(void *L) {
    Game::Lua::CallScript(Offsets::FUN_SCRIPT_FRAME_ENABLEMOUSE, L);
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
    return Game::Lua::CallScript(Offsets::FUN_SCRIPT_FRAME_ISMOUSEENABLED, L);
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
    if (Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, "Usage: self:SetPropagateMouseClicks(propagate)");
        return 0;
    }
    if (Game::Lua::ToFlag(L, 2))
        g_propagatesClicks.insert(frame);
    else
        g_propagatesClicks.erase(frame);
    return 0;
}

// `frame:SetPassThroughButtons(...)`: replaces the list; no names clears it.
// A name that is not a button adds nothing; an argument that is not a string
// raises the usage error (3.80.2's 0x140A9E560).
int __fastcall Script_SetPassThroughButtons(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    uint32_t mask = 0;
    const int top = Game::Lua::GetTop(L);
    for (int i = 2; i <= top; ++i) {
        if (Game::Lua::Type(L, i) != Game::Lua::TYPE_STRING) {
            Game::Lua::Error(L, "Usage: self:SetPassThroughButtons([button1, ...])");
            return 0;
        }
        mask |= Frame::ClickEvents::MaskForButtonName(Game::Lua::ToString(L, i));
    }
    SetPassThrough(frame, mask);
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
    {"EnableMouseMotion", &Script_SetMouseMotionEnabled},
    {"IsMouseClickEnabled", &Script_IsMouseClickEnabled},
    {"IsMouseMotionEnabled", &Script_IsMouseMotionEnabled},
    {"SetPropagateMouseClicks", &Script_SetPropagateMouseClicks},
    {"CanPropagateMouseClicks", &Script_CanPropagateMouseClicks},
    {"SetPassThroughButtons", &Script_SetPassThroughButtons},
};

const Game::Doc::Field kClickArgs[] = {
    Game::Doc::Opt("enabled", "bool", "false", "When true, the frame takes mouse clicks."),
};
const Game::Doc::Field kMotionArgs[] = {
    Game::Doc::Opt("enabled", "bool", "false", "When true, the frame takes mouse motion: OnEnter and OnLeave."),
};
const Game::Doc::Field kEnableMotionArgs[] = {
    Game::Doc::Opt("enable", "bool", "false", "When true, the frame takes mouse motion: OnEnter and OnLeave."),
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
const Game::Doc::Function kEnableMotion{"Sets whether the frame takes mouse motion.", kEnableMotionArgs, {}};
const Game::Doc::Function kIsClick{"Returns whether the frame takes mouse clicks.", {}, kClickRets};
const Game::Doc::Function kIsMotion{"Returns whether the frame takes mouse motion.", {}, kMotionRets};
const Game::Doc::Function kSetPropagate{
    "Sets whether a click on the frame also goes to the next frame under the cursor.", kPropagateArgs, {}};
const Game::Doc::Function kCanPropagate{"Returns whether the frame passes mouse clicks on.", {}, kPropagateRets};
const Game::Doc::Field kPassThroughArgs[] = {
    Game::Doc::Vararg("string",
                      "Any number of button names: LeftButton (Button1), RightButton (Button2), MiddleButton "
                      "(Button3), Button4 to Button31."),
};
const Game::Doc::Function kSetPassThrough{
    "Sets the mouse buttons the frame lets through. A click with one of them goes to the frame under it, or to the "
    "world. No names clears the list.",
    kPassThroughArgs, {}};
const Game::Doc::Field kEnableMouseArgs[] = {
    Game::Doc::Opt("enable", "bool", "false"),
};
const Game::Doc::Function kEnableMouse{"Sets whether the frame takes mouse clicks and motion.",
                                       kEnableMouseArgs, {}};
const Game::Doc::Field kIsMouseEnabledRets[] = {
    Game::Doc::Opt("enabled", "number", nullptr,
                   "1 when the frame takes both clicks and motion, otherwise nil."),
};
const Game::Doc::Function kIsMouseEnabled{
    "Returns whether the frame takes both mouse clicks and mouse motion.", {},
    kIsMouseEnabledRets};

const Game::Doc::Method g_methodDocs[] = {
    {"EnableMouse", &kEnableMouse},
    {"IsMouseEnabled", &kIsMouseEnabled},
    {"SetMouseClickEnabled", &kSetClick},
    {"SetMouseMotionEnabled", &kSetMotion},
    {"EnableMouseMotion", &kEnableMotion},
    {"IsMouseClickEnabled", &kIsClick},
    {"IsMouseMotionEnabled", &kIsMotion},
    {"SetPropagateMouseClicks", &kSetPropagate},
    {"CanPropagateMouseClicks", &kCanPropagate},
    {"SetPassThroughButtons", &kSetPassThrough},
};

void RegisterLuaFunctions() {
    Common::ReplaceWithCall(Offsets::ADDR_UI_PRESS_TARGET, kPressTarget, sizeof kPressTarget,
                            reinterpret_cast<const void *>(&PressTargetThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_PRESS_DISPATCH, kPressDispatch, sizeof kPressDispatch,
                            reinterpret_cast<const void *>(&PressDispatchThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_RELEASE_DISPATCH, kReleaseDispatch, sizeof kReleaseDispatch,
                            reinterpret_cast<const void *>(&ReleaseDispatchThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_PRESS_HOST_GATE, kPressHostGate, sizeof kPressHostGate,
                            reinterpret_cast<const void *>(&PressHostGateThunk));
    Common::ReplaceWithCall(Offsets::ADDR_UI_RELEASE_HOST_GATE, kReleaseHostGate, sizeof kReleaseHostGate,
                            reinterpret_cast<const void *>(&ReleaseHostGateThunk));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool MotionEnabled(void *frame) { return MouseBit(frame) && g_motionOff.count(frame) == 0; }

void ProbePressed(const void *event) {
    if (g_pressed.extras.empty())
        return;
    using Probe_t = int(__thiscall *)(void *frame, const void *event);
    const std::vector<void *> extras = g_pressed.extras;
    for (void *f : extras) {
        // A script may take a frame out, or destroy it, during the probes.
        if (f == nullptr || std::find(g_pressed.extras.begin(), g_pressed.extras.end(), f) == g_pressed.extras.end())
            continue;
        const void *vtable = Game::Read<const void *>(f, 0);
        Game::Read<Probe_t>(vtable, Offsets::VTBL_FRAME_ON_MOUSE_MOVE)(f, event);
    }
}

void DropRegion(void *region) { Forget(region); }

void OnInputRemoved(void *frame) {
    g_pressed.extras.erase(std::remove(g_pressed.extras.begin(), g_pressed.extras.end(), frame),
                           g_pressed.extras.end());
}

} // namespace Frame::MouseInput
