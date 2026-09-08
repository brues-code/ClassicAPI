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

#include "frame/RunnerHook.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::RunnerHook {

namespace {

// Chained at static-init time by each `AutoSubscribe` constructor, before
// `DllMain`. Zero-initialized (namespace static) ahead of any dynamic init, so
// the head is valid when subscribers link in.
AutoSubscribe *g_head = nullptr;

using Runner_t = void(__cdecl *)(void *frame, uint32_t *slotPtr, const char *fmt,
                                 void *varargs);
Runner_t g_orig = nullptr;

void __cdecl Hook(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs) {
    for (AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        if (s->fn(frame, slotPtr, fmt, varargs))
            return; // handled — suppressed, or the subscriber ran Original itself
    g_orig(frame, slotPtr, fmt, varargs);
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT, reinterpret_cast<void *>(&Hook),
    reinterpret_cast<void **>(&g_orig)};

} // namespace

AutoSubscribe::AutoSubscribe(Interceptor f) : fn(f), next(g_head) { g_head = this; }

void Original(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs) {
    g_orig(frame, slotPtr, fmt, varargs);
}

} // namespace Frame::RunnerHook
