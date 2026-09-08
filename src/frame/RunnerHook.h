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

#include <cstdint>

// Shared interceptor for the engine's arg'd frame-script runner
// `FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT` (0x00702710) — `__cdecl(frame, slotPtr,
// fmt, vaPtr)`: stamps the exec context from `slotPtr[1]`, runs the handler in
// `slotPtr[0]` through FUN_FRAME_RUN_SCRIPT_ARGS, restores. Exactly two engine
// paths funnel through it: every arg'd input-script fire (OnClick,
// OnMouseWheel, … via the variadic forwarder FUN_007026F0) and the event
// dispatcher FUN_FIRE_EVENT's per-frame OnEvent fire, which passes
// `slotPtr == frame + OFF_FRAME_ONEVENT_SLOT`.
//
// MinHook allows ONE hook per address, and more than one feature needs this
// runner: `Frame::ClickEvents` brackets OnClick with PreClick/PostClick, and
// `Frame::UnitEvent` suppresses OnEvent fires that a RegisterUnitEvent filter
// rejects. So — exactly like `Event::SignalHook` and `Tick::WorldTick` — this
// module owns the single hook and fans out. Declare a file-scope
//   static const Frame::RunnerHook::AutoSubscribe _sub{&interceptor};
//
// An interceptor returns `true` when it has fully handled the fire — either
// suppressed it, or already ran the engine itself through `Original` (the
// bracketing case). `false` falls through to the next subscriber and finally
// the original. The FIRST `true` wins, so subscribers must claim disjoint slot
// addresses; subscriber order is static-init order and must not matter.

namespace Frame::RunnerHook {

using Interceptor = bool (*)(void *frame, uint32_t *slotPtr, const char *fmt,
                             void *varargs);

struct AutoSubscribe {
    explicit AutoSubscribe(Interceptor fn);
    Interceptor fn;
    AutoSubscribe *next;
};

// The engine's runner (the MinHook trampoline) — for interceptors that run the
// fire themselves, e.g. to bracket it, rather than suppress it. Valid only once
// hooks are installed, i.e. from inside an interceptor.
void Original(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs);

} // namespace Frame::RunnerHook
