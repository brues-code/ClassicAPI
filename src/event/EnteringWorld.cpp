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

// `PLAYER_ENTERING_WORLD(isInitialLogin, isReloadingUi)` — the 8.0.1 payload,
// backported. Vanilla fires PEW with no args; backported addons expect the
// two booleans to distinguish a fresh login from a `/reload` from an
// instance/zone transition.
//
// Delivery: PEW is a no-arg event, so the engine broadcasts it through
// `FUN_FIRE_EVENT_NO_ARGS` (the `__fastcall(int)` dispatcher), NOT the
// variadic `FUN_FIRE_EVENT`. We intercept that dispatcher (via the shared
// `Event::SignalHook`) and, for the PEW event id, suppress the no-arg fire
// and instead re-broadcast through the variadic `FUN_FIRE_EVENT` with a
// two-boolean payload. Both fire paths read the same frame chain off the
// event's table slot, so the frames registered for PEW receive the args
// exactly once. Booleans use the `%d`/`%s`+nullptr trick (`1` / `nil`), so
// `if isInitialLogin then …` reads naturally.
//
// Flags, from signals we already produce — no PEW-specific engine hook:
//   - isInitialLogin: our GlueModuleAutoRegister runs on every glue-screen
//     boot (initial launch + each logout return), so a set latch means "the
//     next enter-world is a fresh login". The first PEW consumes it.
//   - isReloadingUi: our ModuleAutoRegister runs whenever the in-game Lua
//     state is (re)built — login OR `/reload`, but NOT a zone transition. So
//     "script re-init happened and it wasn't the fresh login" == a reload.
//   - Instance/zone transition rebuilds neither, so both latches are clear →
//     both false, matching retail.

#include "event/EnteringWorld.h"

#include "Game.h"
#include "event/Custom.h"
#include "event/SignalHook.h"

namespace Event::EnteringWorld {

namespace {

// Chained at static init by each `AutoObserve`, before `DllMain`.
const AutoObserve *g_observers = nullptr;

// Set on glue-screen boot; a pending latch means the next enter-world is a
// fresh character login (from the login/char-select screen).
bool g_atGlue = false;
// Set when the in-game Lua state is (re)built (login or /reload).
bool g_scriptReloaded = false;
// Cached PLAYER_ENTERING_WORLD event-table slot; -1 = unresolved. Reset per
// in-game init (the table is rebuilt, though engine slots are stable) and
// re-resolved lazily at fire time.
int g_pewId = -1;

int PewId() {
    if (g_pewId < 0)
        g_pewId = Event::Custom::LookupByName("PLAYER_ENTERING_WORLD");
    return g_pewId;
}

// Shared-hook interceptor. For PEW, re-broadcast with the payload and
// suppress the engine's no-arg fire; everything else passes through.
bool Intercept(int eventID) {
    const int pew = PewId();
    if (pew < 0 || eventID != pew)
        return false;

    const bool isInitialLogin = g_atGlue;
    g_atGlue = false;
    const bool isReloadingUi = !isInitialLogin && g_scriptReloaded;
    g_scriptReloaded = false;

    // Re-fire through the variadic dispatcher with the two booleans. true → 1
    // (%d), false → nil (%s + nullptr, which the dispatcher's pushstring
    // tail-jumps to pushnil).
    auto *const kNil = static_cast<const char *>(nullptr);
    if (isInitialLogin && isReloadingUi)
        Event::Custom::Fire(pew, "%d%d", 1, 1);
    else if (isInitialLogin)
        Event::Custom::Fire(pew, "%d%s", 1, kNil);
    else if (isReloadingUi)
        Event::Custom::Fire(pew, "%s%d", kNil, 1);
    else
        Event::Custom::Fire(pew, "%s%s", kNil, kNil);

    for (const AutoObserve *o = g_observers; o != nullptr; o = o->next)
        o->fn();
    return true; // handled — suppress the original no-arg PEW fire
}

const Event::SignalHook::AutoSubscribe _intercept{&Intercept};

// In-game Lua (re)init: login or /reload rebuilt the script state (never a
// zone transition). Also re-arm the PEW slot lookup — the event table was
// rebuilt.
void OnInGameInit() {
    g_scriptReloaded = true;
    g_pewId = -1;
}

// Glue-screen boot: we're at the login/char-select screen, so the next
// enter-world will be a fresh login.
void OnGlueInit() { g_atGlue = true; }

const Game::ModuleAutoRegister _inGame{&OnInGameInit};
const Game::GlueModuleAutoRegister _glue{&OnGlueInit};

} // namespace

AutoObserve::AutoObserve(Observer f) : fn(f), next(g_observers) { g_observers = this; }

} // namespace Event::EnteringWorld
