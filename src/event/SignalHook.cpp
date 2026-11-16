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

#include "SignalHook.h"

#include "Game.h"
#include "Offsets.h"

namespace Event::SignalHook {

namespace {

// Chained at static-init time by each `AutoSubscribe` constructor, before
// `DllMain`. Zero-initialized (namespace static) ahead of any dynamic init,
// so the head is valid when subscribers link in.
AutoObserve *g_observers = nullptr;

// `FUN_FIRE_EVENT_NO_ARGS` is `__fastcall(int eventID)`; we mirror loot's
// long-standing shape with a dummy EDX so the trampoline call matches the
// register layout, and forward `0` for it (unused by the callee).
using FireNoArgs_t = void(__fastcall *)(int eventID, int edx);
FireNoArgs_t g_orig = nullptr;

void __fastcall Hook(int eventID, int /*edx*/) {
    for (AutoObserve *o = g_observers; o != nullptr; o = o->next)
        if (o->eventID == eventID)
            o->fn();
    for (AutoSubscribe *s = AutoSubscribe::head; s != nullptr; s = s->next)
        if (s->fn(eventID))
            return; // a subscriber handled it — suppress the original fire
    g_orig(eventID, 0);
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_FIRE_EVENT_NO_ARGS, reinterpret_cast<void *>(&Hook),
    reinterpret_cast<void **>(&g_orig)};

} // namespace

AutoObserve::AutoObserve(int id, void (*f)()) : eventID(id), fn(f), next(g_observers) {
    g_observers = this;
}

} // namespace Event::SignalHook
