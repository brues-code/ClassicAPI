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

#include "AutoList.h"

#include "Game.h"

#include <cstdint>

namespace Frame::ScriptArgs {

// Runs a frame-script handler with arbitrary Lua values as its arguments —
// the engine's runners pass only what a printf-style format can carry (no
// booleans). `nargs` values at absolute
// stack index `firstArgIdx` become `arg1..argN` (globals, saved and restored
// around the call) and, while modern script args are on, the positional
// `(self, ...)`. Sets `this`, runs under the engine's script error handler,
// and leaves the stack as it found it. For scripts the engine itself never
// fires, such as OnAttributeChanged (4.3.4: RunScript with 2 stack args).
void RunWithValues(void *L, int handlerRef, void *frame, int firstArgIdx, int nargs, bool *returnedTrue = nullptr);

// RunWithValues for a script slot {handler, context}, inside the slot's
// execution context: VAR_FRAMESCRIPT_EXEC_CONTEXT is set from slot[1] for the
// call and restored after, with the depth count kept, as the engine's own
// fire wrapper 0x00702690 does for OnTextChanged, OnTextSet and the other
// scripts it runs. Does nothing for an empty slot.
//
// A non-null `returnedTrue` (both functions) is set to whether the handler
// returned the boolean true, for scripts whose return value the engine reads,
// such as OnGamePadStick (3.80.2's 0x1405A1190); false when nothing ran.
void RunSlotWithValues(void *L, const uint32_t *slot, void *frame, int firstArgIdx, int nargs,
                       bool *returnedTrue = nullptr);

// RunSlotWithValues for a fire from C code: nothing runs for a null or empty
// slot or without a Lua state; otherwise `push(L)` pushes the handler's
// arguments and returns how many, and the Lua top is restored after, so a
// fire from inside running engine code leaves its stack as it was.
template <class Push>
void RunSlot(const uint32_t *slot, void *frame, Push &&push, bool *returnedTrue = nullptr) {
    if (returnedTrue != nullptr)
        *returnedTrue = false;
    if (slot == nullptr || slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    const int nargs = push(L);
    RunSlotWithValues(L, slot, frame, top + 1, nargs, returnedTrue);
    Game::Lua::SetTop(L, top);
}

// RunSlot with no arguments.
inline void RunSlot(const uint32_t *slot, void *frame) {
    RunSlot(slot, frame, [](void *) { return 0; });
}

// Every frame-script handler run passes through this module — the engine's
// two runners (FUN_FRAME_INVOKE_SCRIPT, FUN_FRAME_RUN_SCRIPT_ARGS, co-hooked
// here) and RunWithValues — so it plays 3.3.5's FrameScript_Execute, the one
// function that runs a script. An observer is called with `begin` true
// before the handler runs and false after, nested runs nesting. Declare a
// file-scope `static const Frame::ScriptArgs::AutoObserve`.
using Observer = void (*)(bool begin, int handlerRef, void *frame);

using AutoObserve = AutoList<struct AutoObserveTag, Observer>;

// A boolean 3.3.5 passes after a script's values, which 1.12's runners
// cannot carry: OnClick/PreClick/PostClick's `down`, OnEnter/OnLeave's
// `motion`. A provider returns true and sets `value` when the run of
// `handlerRef` on `frame` takes one; the first provider that does wins. It
// becomes the last argN and positional argument of a handler that takes the
// positional path; a handler that declares no parameters keeps vanilla's
// globals. Declare a file-scope `static const Frame::ScriptArgs::AutoTrailingBool`.
using TrailingBool = bool (*)(int handlerRef, void *frame, bool *value);

using AutoTrailingBool = AutoList<struct AutoTrailingBoolTag, TrailingBool>;

} // namespace Frame::ScriptArgs
