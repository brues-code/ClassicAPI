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

// Scripted input on every ScriptRegion: FocusEnter([fromMouseMotion]),
// FocusExit([fromMouseMotion]), MouseDown([button]) and MouseUp([button]).
// Only `_forever_` documents them (no 3.80.2 counterpart;
// docs/notes/gamepad-input-region-api-3802.md §4). A gamepad UI drives the
// interface with them: its navigation runs FocusExit on the button it leaves
// and FocusEnter on the one it selects (the highlight it drives through
// SetHighlightLocked), and a tab or dropdown that opens from OnMouseDown is
// clicked with MouseDown then MouseUp.
//
// Frames run the engine's own notify and button slots, so each does what the
// mouse does to that frame type:
//   - FocusEnter: the enter notify (VTBL_FRAME_ON_ENTER). A button skips it
//     while disabled, else lights its highlight layer, runs OnEnter and
//     refreshes its label;
//   - FocusExit: the leave notify (VTBL_FRAME_ON_LEAVE, not a removal). The
//     highlight goes off unless it is locked;
//   - MouseDown / MouseUp: the button-down and button-up slots
//     (VTBL_FRAME_ON_MOUSE_DOWN / _UP) with a mouse event built as the
//     press handlers build theirs: the context's last mouse payload through
//     the engine's converter, the button's bit, the held mask (the bit on
//     the down, none on the up), and the cursor at the centre of the frame's
//     hit rect, so a button's up-inside test passes and the pair clicks it.
//     The context's pressed frame is not touched.
// `fromMouseMotion` (default true) is the OnEnter / OnLeave `motion`
// argument (Frame::EnterLeaveMotion::Offer). `button` (default
// "LeftButton") is read as a mouse-button name; the scripts get the
// engine's name for its bit.
// Textures and font strings run their own scripts (Frame::RegionScripts):
// OnEnter(motion), OnLeave(motion), OnMouseDown(button) and
// OnMouseUp(button, true).
// Nothing changes the motion focus (GetMouseFoci) or the press in progress.

#include "Game.h"
#include "Offsets.h"
#include "frame/ClickEvents.h"
#include "frame/EnterLeaveMotion.h"
#include "frame/Layout.h"
#include "frame/RegionScripts.h"
#include "lua/Arg.h"

#include <cstdint>
#include <cstring>

namespace Frame::ScriptedInput {

namespace {

namespace Arg = Game::Lua::Arg;
using Frame::RegionScripts::IsRegion;

using Enter_t = void(__thiscall *)(void *frame);
using Leave_t = void(__thiscall *)(void *frame, int removal);
using Button_t = void(__thiscall *)(void *frame, const void *event);
using CopyEvent_t = void *(__thiscall *)(void *copy, const void *payload);

void RunMotion(void *object, bool enter, bool motion) {
    if (IsRegion(object)) {
        Frame::RegionScripts::Run(object, enter ? Frame::RegionScripts::ON_ENTER : Frame::RegionScripts::ON_LEAVE,
                                  [motion](void *L) {
                                      Game::Lua::PushBool(L, motion);
                                      return 1;
                                  });
        return;
    }
    const Frame::EnterLeaveMotion::Offer offer(object, motion);
    const void *vtable = Game::Read<const void *>(object, 0);
    if (enter)
        Game::Read<Enter_t>(vtable, Offsets::VTBL_FRAME_ON_ENTER)(object);
    else
        Game::Read<Leave_t>(vtable, Offsets::VTBL_FRAME_ON_LEAVE)(object, 0);
}

// The press or release copy OnMouseDown / OnMouseUp build, at the frame.
void BuildEvent(uint8_t (&event)[Offsets::UI_MOUSE_EVENT_SIZE], const void *frame, bool down, uint32_t bit) {
    std::memset(event, 0, sizeof event);
    Game::Ref<uint32_t>(event, 0) = Offsets::VTBL_UI_MOUSE_EVENT;
    Game::Ref<uint32_t>(event, Offsets::OFF_UI_MOUSE_EVENT_ID) =
        down ? Offsets::UI_MOUSE_EVENT_ID_DOWN : Offsets::UI_MOUSE_EVENT_ID_UP;
    if (const uint8_t *ctx = Game::Read<const uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR))
        reinterpret_cast<CopyEvent_t>(Offsets::FUN_UI_COPY_MOUSE_EVENT)(
            event, ctx + Offsets::OFF_UI_CONTEXT_SAVED_MOUSE_EVENT);
    Game::Ref<uint32_t>(event, Offsets::OFF_UI_MOUSE_EVENT_BUTTON) = bit;
    Game::Ref<uint32_t>(event, Offsets::OFF_MOUSE_EVENT_BUTTONS) = down ? bit : 0;
    const Frame::Layout::Rect &r = Frame::Layout::HitRectOf(frame);
    Game::Ref<float>(event, Offsets::OFF_UI_MOUSE_EVENT_X) = (r.left + r.right) * 0.5f;
    Game::Ref<float>(event, Offsets::OFF_UI_MOUSE_EVENT_Y) = (r.bottom + r.top) * 0.5f;
}

void RunButton(void *object, bool down, uint32_t bit) {
    if (IsRegion(object)) {
        const char *name = Frame::ClickEvents::ButtonNameForMask(bit);
        if (down)
            Frame::RegionScripts::Run(object, Frame::RegionScripts::ON_MOUSE_DOWN, [name](void *L) {
                Game::Lua::PushString(L, name);
                return 1;
            });
        else
            Frame::RegionScripts::Run(object, Frame::RegionScripts::ON_MOUSE_UP, [name](void *L) {
                Game::Lua::PushString(L, name);
                Game::Lua::PushBool(L, true);
                return 2;
            });
        return;
    }
    uint8_t event[Offsets::UI_MOUSE_EVENT_SIZE];
    BuildEvent(event, object, down, bit);
    const void *vtable = Game::Read<const void *>(object, 0);
    Game::Read<Button_t>(vtable, down ? Offsets::VTBL_FRAME_ON_MOUSE_DOWN : Offsets::VTBL_FRAME_ON_MOUSE_UP)(object,
                                                                                                         event);
}

int Focus(void *L, bool enter, const char *usage) {
    void *object = Game::Lua::ResolveObject(L, 1);
    if (object == nullptr)
        return 0;
    bool motion = true;
    if (!Arg::Bool(L, 2, usage, motion, true))
        return 0;
    RunMotion(object, enter, motion);
    return 0;
}

int Button(void *L, bool down, const char *usage) {
    void *object = Game::Lua::ResolveObject(L, 1);
    if (object == nullptr)
        return 0;
    Arg::Text button{"LeftButton", 10};
    if (!Arg::String(L, 2, usage, button, true))
        return 0;
    RunButton(object, down, Frame::ClickEvents::MaskForButtonName(button.data));
    return 0;
}

int __fastcall Script_FocusEnter(void *L) { return Focus(L, true, "Usage: self:FocusEnter([fromMouseMotion])"); }
int __fastcall Script_FocusExit(void *L) { return Focus(L, false, "Usage: self:FocusExit([fromMouseMotion])"); }
int __fastcall Script_MouseDown(void *L) { return Button(L, true, "Usage: self:MouseDown([button])"); }
int __fastcall Script_MouseUp(void *L) { return Button(L, false, "Usage: self:MouseUp([button])"); }

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"FocusEnter", &Script_FocusEnter},
    {"FocusExit", &Script_FocusExit},
    {"MouseDown", &Script_MouseDown},
    {"MouseUp", &Script_MouseUp},
};

const Game::Doc::Field kMotionArgs[] = {
    Game::Doc::Opt("fromMouseMotion", "bool", "true", "The motion argument OnEnter and OnLeave get."),
};
const Game::Doc::Field kButtonArgs[] = {
    Game::Doc::Opt("button", "cstring", "\"LeftButton\""),
};
const Game::Doc::Function kFocusEnter{
    "Runs what the cursor entering the object runs: OnEnter, and a button's highlight and label.", kMotionArgs, {}};
const Game::Doc::Function kFocusExit{
    "Runs what the cursor leaving the object runs: OnLeave, and a button's highlight unless it is locked.",
    kMotionArgs, {}};
const Game::Doc::Function kMouseDown{"Presses a mouse button on the object, as a click on its centre does.",
                                     kButtonArgs, {}};
const Game::Doc::Function kMouseUp{
    "Releases a mouse button on the object, as a click on its centre does. After MouseDown it clicks a button.",
    kButtonArgs, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"FocusEnter", &kFocusEnter},
    {"FocusExit", &kFocusExit},
    {"MouseDown", &kMouseDown},
    {"MouseUp", &kMouseUp},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::ScriptedInput
