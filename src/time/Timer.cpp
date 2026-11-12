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

#include "time/Timer.h"

#include "Game.h"
#include "tick/WorldTick.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <windows.h>

namespace Time::Timer {

// Engine-bound `C_Timer.After` / `NewTimer` / `NewTicker` — the
// modern API for one-shot and recurring callbacks. Retail 12.0.5
// implements these as engine C functions (not Lua, contrary to the
// older Lua-in-FrameXML implementation); we mirror that shape.
//
// Architecture:
//   - One timer queue (`g_timers`) walked once per frame from the
//     `WorldTick` subscription, no other hooks.
//   - Callbacks are pinned in the Lua registry under per-timer
//     string keys so they survive GC between scheduling and firing.
//   - The cancel handle returned by NewTimer / NewTicker is a Lua
//     table with `Cancel` / `IsCancelled` methods bound to C
//     closures that capture the timer ID as an upvalue (kept off
//     the handle's public field set, so callers can't accidentally
//     reach into the internal state).
//   - Re-entrancy: a callback that schedules a new timer is safe —
//     the new entry is appended after this tick's snapshot. A
//     callback that cancels another pending timer is also safe; we
//     re-check the cancel flag inside the fire pass.
//   - Owners (Timer.h): a ModelScene's timers share the queue, the
//     handles and the callback storage, with deadlines on the scene's
//     clock; the scene fires its own, the WorldTick only C_Timer's.

namespace {

constexpr double TIMER_MIN_PERIOD = 0.0001; // tickers with period<=0 would tight-loop

struct Entry {
    int id;
    const void *owner;      // null = C_Timer; else the object whose clock this is on
    double deadline;        // seconds on the owner's clock, absolute
    double period;          // 0 = one-shot; >0 = ticker period
    int iterationsLeft;     // -1 = infinite; 0 = mark for removal; >0 = countdown
    bool cancelled;
};

std::vector<Entry> g_timers;
int g_nextID = 1;

// The earliest deadline of a live C_Timer timer: a frame before it has
// nothing to fire and returns at once. Scheduling lowers it; the sweep at the end of a
// firing frame recomputes it. A timer cancelled first only costs one early
// pass that fires nothing.
double g_nextDeadline = HUGE_VAL;

// IDs due this frame (Tick pass 1), kept for its capacity.
std::vector<int> g_toFire;

// ----- Monotonic clock -------------------------------------------------

LARGE_INTEGER g_qpcFreq{};
LARGE_INTEGER g_qpcStart{};

double Now() {
    if (g_qpcFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&g_qpcFreq);
        QueryPerformanceCounter(&g_qpcStart);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - g_qpcStart.QuadPart) /
           static_cast<double>(g_qpcFreq.QuadPart);
}

// ----- Lua registry callback storage -----------------------------------

void MakeKey(int id, char (&buf)[32]) {
    std::snprintf(buf, sizeof(buf), "ClassicAPI.Timer.%d", id);
}

// Assumes the callback is on top of the Lua stack; pops it.
void StoreCallback(void *L, int id) {
    char key[32];
    MakeKey(id, key);
    Game::Lua::PushString(L, key);  // stack: ..., callback, key
    Game::Lua::Insert(L, -2);        // stack: ..., key, callback
    Game::Lua::SetTable(L, Game::Lua::REGISTRY_INDEX);
}

void PushCallback(void *L, int id) {
    char key[32];
    MakeKey(id, key);
    Game::Lua::PushString(L, key);
    Game::Lua::GetTable(L, Game::Lua::REGISTRY_INDEX);
}

void ReleaseCallback(void *L, int id) {
    char key[32];
    MakeKey(id, key);
    Game::Lua::PushString(L, key);
    Game::Lua::PushNil(L);
    Game::Lua::SetTable(L, Game::Lua::REGISTRY_INDEX);
}

// ----- Cancel-handle methods -------------------------------------------

Entry *FindByID(int id) {
    for (auto &t : g_timers) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

int __fastcall Script_TimerCancel(void *L) {
    if (!Game::Lua::IsNumber(L, Game::Lua::UpvalueIndex(1)))
        return 0;
    const int id = static_cast<int>(Game::Lua::ToNumber(L, Game::Lua::UpvalueIndex(1)));
    if (Entry *t = FindByID(id))
        t->cancelled = true;
    return 0;
}

int __fastcall Script_TimerIsCancelled(void *L) {
    bool cancelled = true; // "no longer alive" → cancelled
    if (Game::Lua::IsNumber(L, Game::Lua::UpvalueIndex(1))) {
        const int id = static_cast<int>(Game::Lua::ToNumber(L, Game::Lua::UpvalueIndex(1)));
        if (Entry *t = FindByID(id))
            cancelled = t->cancelled;
    }
    Game::Lua::PushBool(L, cancelled);
    return 1;
}

// Pushes a `{ Cancel = <c>, IsCancelled = <c> }` table onto the
// stack. Each method binds `id` as a closure upvalue.
void PushCancelHandle(void *L, int id) {
    Game::Lua::NewTable(L);

    Game::Lua::PushString(L, "Cancel");
    Game::Lua::PushNumber(L, static_cast<double>(id));
    Game::Lua::PushCClosure(L, &Script_TimerCancel, 1);
    Game::Lua::SetTable(L, -3);

    Game::Lua::PushString(L, "IsCancelled");
    Game::Lua::PushNumber(L, static_cast<double>(id));
    Game::Lua::PushCClosure(L, &Script_TimerIsCancelled, 1);
    Game::Lua::SetTable(L, -3);
}

// ----- Fire ready timers -----------------------------------------------

void FireDue(const void *owner, double now) {
    if (g_timers.empty())
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;

    // Pass 1: snapshot IDs of the owner's timers due to fire. Walking by
    // index would invalidate on re-entrant scheduling; capture IDs and
    // re-lookup inside the fire pass.
    g_toFire.clear();
    for (const auto &t : g_timers) {
        if (t.owner == owner && !t.cancelled && t.iterationsLeft != 0 && t.deadline <= now)
            g_toFire.push_back(t.id);
    }

    // Pass 2: fire each. Re-check cancellation per timer in case a
    // prior callback in this tick cancelled it.
    for (int id : g_toFire) {
        Entry *t = FindByID(id);
        if (t == nullptr || t->cancelled)
            continue;

        const int topBefore = Game::Lua::GetTop(L);
        PushCallback(L, id);
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
            // Callback ref disappeared — release and drop.
            Game::Lua::SetTop(L, topBefore);
            t->cancelled = true;
            continue;
        }
        // Errors in the callback are swallowed — we deliberately
        // don't propagate, matching modern WoW's behavior where a
        // broken `C_Timer.After` callback doesn't poison the next
        // tick's other timers.
        Game::Lua::PCall(L, 0, 0, 0);
        Game::Lua::SetTop(L, topBefore);

        // Re-find — callback might have cancelled `t` (or scheduled
        // others, which we don't care about here).
        t = FindByID(id);
        if (t == nullptr || t->cancelled)
            continue;

        if (t->period > 0.0) {
            t->deadline += t->period;
            if (t->iterationsLeft > 0)
                t->iterationsLeft -= 1;
            if (t->iterationsLeft == 0)
                t->cancelled = true;
        } else {
            t->cancelled = true;
        }
    }

    // Pass 3: sweep cancelled entries and release their registry refs, in
    // one compacting pass, and find the next C_Timer deadline among the rest.
    size_t keep = 0;
    g_nextDeadline = HUGE_VAL;
    for (const Entry &t : g_timers) {
        if (t.cancelled) {
            ReleaseCallback(L, t.id);
            continue;
        }
        if (t.owner == nullptr && t.deadline < g_nextDeadline)
            g_nextDeadline = t.deadline;
        g_timers[keep++] = t;
    }
    g_timers.resize(keep);
}

void Tick() {
    const double now = Now();
    if (now < g_nextDeadline)
        return;
    FireDue(nullptr, now);
}

// ----- Scheduling --------------------------------------------------------

int Schedule(void *L, int callbackStackIdx, const void *owner, double now, double seconds,
             double period, int iterations) {
    const int id = g_nextID++;
    Game::Lua::PushValue(L, callbackStackIdx);
    StoreCallback(L, id);

    Entry e{};
    e.id = id;
    e.owner = owner;
    e.deadline = now + (seconds < 0 ? 0 : seconds);
    e.period = period;
    e.iterationsLeft = iterations;
    e.cancelled = false;
    g_timers.push_back(e);
    if (owner == nullptr && e.deadline < g_nextDeadline)
        g_nextDeadline = e.deadline;
    return id;
}

// ----- Lua surface -------------------------------------------------------

int __fastcall Script_C_Timer_After(void *L) {
    return Script(L, 1, nullptr, Now(), Kind::After, "Usage: C_Timer.After(seconds, callback)");
}

int __fastcall Script_C_Timer_NewTimer(void *L) {
    return Script(L, 1, nullptr, Now(), Kind::NewTimer,
                  "Usage: local cbObject = C_Timer.NewTimer(seconds, callback)");
}

int __fastcall Script_C_Timer_NewTicker(void *L) {
    return Script(L, 1, nullptr, Now(), Kind::NewTicker,
                  "Usage: local cbObject = C_Timer.NewTicker(seconds, callback [, iterations])");
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Timer", "After", &Script_C_Timer_After);
    Game::Lua::RegisterTableFunction("C_Timer", "NewTimer", &Script_C_Timer_NewTimer);
    Game::Lua::RegisterTableFunction("C_Timer", "NewTicker", &Script_C_Timer_NewTicker);
}

} // namespace

int Script(void *L, int first, const void *owner, double now, Kind kind, const char *usage) {
    if (!Game::Lua::IsNumber(L, first) || Game::Lua::Type(L, first + 1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "%s", usage);
        return 0;
    }
    double seconds = Game::Lua::ToNumber(L, first);
    if (kind != Kind::NewTicker) {
        const int id = Schedule(L, first + 1, owner, now, seconds, /*period=*/0.0, /*iterations=*/1);
        if (kind == Kind::After)
            return 0;
        PushCancelHandle(L, id);
        return 1;
    }
    if (seconds < TIMER_MIN_PERIOD)
        seconds = TIMER_MIN_PERIOD;
    int iterations = -1; // infinite by default
    if (Game::Lua::IsNumber(L, first + 2)) {
        const double v = Game::Lua::ToNumber(L, first + 2);
        if (v >= 1.0)
            iterations = static_cast<int>(v);
        // negative / zero / non-numeric → keep -1 (infinite), matching
        // modern WoW's behavior where omitted iterations means forever.
    }
    const int id = Schedule(L, first + 1, owner, now, seconds, /*period=*/seconds, iterations);
    PushCancelHandle(L, id);
    return 1;
}

void Fire(const void *owner, double now) { FireDue(owner, now); }

void CancelOwned(const void *owner) {
    void *L = Game::Lua::State();
    size_t keep = 0;
    for (const Entry &t : g_timers) {
        if (t.owner == owner) {
            if (L != nullptr)
                ReleaseCallback(L, t.id);
            continue;
        }
        g_timers[keep++] = t;
    }
    g_timers.resize(keep);
}

// Cancel every pending timer and release its Lua registry callback before a
// /reload (or logout) resets the Lua state. Otherwise g_timers would carry
// entries whose "ClassicAPI.Timer.N" registry callbacks died with the reset:
// the WorldTick fire pass drops a nil callback safely, but a surviving/stale
// registry ref could fire a dead closure. Clearing here is the consistent,
// correct behavior — a reload cancels pending timers, matching modern WoW,
// where C_Timer callbacks are Lua closures that don't survive the reset.
void PrepareForReload() {
    if (void *L = Game::Lua::State())
        for (const auto &t : g_timers)
            ReleaseCallback(L, t.id);
    g_timers.clear();
    g_nextID = 1;
    g_nextDeadline = HUGE_VAL;
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
static const Tick::WorldTick::AutoSubscribe _tick{&Tick};

} // namespace Time::Timer
