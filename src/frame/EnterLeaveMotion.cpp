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

// OnEnter / OnLeave `motion` — `function(self, motion)`, as 3.3.5 passes it.
//
// 3.3.5's CSimpleTop::OnMouseMove (0x004945A0) computes
// `motion = mouseData != &top->m_mousePosition`: true when a real mouse move
// changed the focus, false when the focus was picked again at the saved cursor
// position because something moved under a still cursor. It hands that to the
// old focus's OnLayerCursorExit and the new focus's OnLayerCursorEnter. Every
// other enter/leave — SetMouseFocus, a frame hidden or disabled under the
// cursor — passes 0.
//
// 1.12's focus pass FUN_UI_MOUSE_FOCUS_PASS (0x007660D0) is the same function:
// the layout update re-runs it with the context's saved event copy
// (+OFF_UI_CONTEXT_SAVED_MOUSE_EVENT), and `cmp ebx,edi` at 0x00766114 tells
// the two apart. Its enter/leave notifies take no motion, so the pass is
// co-hooked to record {context, motion} while it runs, and the OnEnter/OnLeave
// runs inside it are offered `motion` as Frame::ScriptArgs' trailing boolean.
// The pass's own notifies are the ones Frame::MotionFocus runs for the frames
// that leave and join the focus, and it names the frame it is notifying, so
// a nested enter/leave a handler causes — a frame hidden under the cursor —
// is told apart from the pass's own and gets false, as in 3.3.5.

#include "Game.h"
#include "Offsets.h"
#include "frame/MotionFocus.h"
#include "frame/ScriptArgs.h"

namespace Frame::EnterLeaveMotion {

namespace {

// The focus pass in progress; context null outside one.
struct Pass {
    void *context;
    bool motion;
};
Pass g_pass = {nullptr, false};

using FocusPass_t = int(__fastcall *)(const void *event, void *context);
FocusPass_t g_origFocusPass = nullptr;

int __fastcall FocusPass_h(const void *event, void *context) {
    const Pass saved = g_pass;
    g_pass.context = context;
    g_pass.motion =
        context != nullptr &&
        event != static_cast<const uint8_t *>(context) + Offsets::OFF_UI_CONTEXT_SAVED_MOUSE_EVENT;
    const int result = g_origFocusPass(event, context);
    g_pass = saved;
    return result;
}

const Game::HookAutoRegister _focusPassHook{
    Offsets::FUN_UI_MOUSE_FOCUS_PASS,
    reinterpret_cast<void *>(&FocusPass_h),
    reinterpret_cast<void **>(&g_origFocusPass)};

// Every OnEnter/OnLeave run takes `motion`; it is true only for the pass's
// own notifies on a real mouse move.
bool Motion(int handlerRef, void *frame, bool *value) {
    if (frame == nullptr)
        return false;
    const bool enter = Game::Read<int>(frame, Offsets::OFF_FRAME_ONENTER_SLOT) == handlerRef;
    if (!enter && Game::Read<int>(frame, Offsets::OFF_FRAME_ONLEAVE_SLOT) != handlerRef)
        return false;
    *value = g_pass.context != nullptr && g_pass.motion && frame == Frame::MotionFocus::Notifying();
    return true;
}

const Frame::ScriptArgs::AutoTrailingBool _motion{&Motion};

} // namespace

} // namespace Frame::EnterLeaveMotion
