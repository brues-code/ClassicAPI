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
#include "event/FunctionContainer.h"
#include "lua/Arg.h"
#include "profile/AddOnProfiler.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace Time::Timer {

// `C_Timer.After` / `NewTimer` / `NewTicker` as the 3.80.2 client has them
// (FUN_140e15210 / FUN_140e15800 / FUN_140e154a0; queue FUN_141bad100,
// FUN_141bad210, tick FUN_141bac9a0). docs/notes/c-timer-3802.md has the read.
//
//   - Arguments: seconds by the FiniteNumber reader, the callback a Lua
//     function or a C_FunctionContainers container (else the usage error at
//     its argument), NewTicker's iterations an optional uint32.
//   - A negative duration or one over 4294967.295 seconds makes no timer and
//     raises "<Fn>(): <api> requires a non-negative duration" / "<api>
//     duration is too large", where <api> is "C_Timer.NewTicker" for NewTimer
//     too. 3.80.2 adds a "\nNo Lua Taint" line; there is no taint here.
//   - Deadlines are whole milliseconds: now + (seconds * 1000) truncated, on
//     the engine's ms tick (Time::Clock, GetTime's epoch) summed so it never
//     wraps. 3.80.2's int cast turns durations over 2147483.647 seconds
//     negative; ours does not.
//   - A 0 ms timer waits until the clock has moved past the moment it was
//     made, then fires; a 0 ms ticker then fires every pass (3.80.2's pending
//     and every-frame lists). The every-frame timers fire first, then the
//     others by deadline.
//   - The callback holds a container (a plain function gets a new one).
//     After's callback gets no arguments; NewTimer's and NewTicker's get
//     their container, a new userdata each call. Errors go to the error
//     handler. A cancelled container does not fire.
//   - NewTimer is a ticker of one iteration. A ticker that has fired is
//     dropped when its container is cancelled or its count is used up (the
//     container stays uncancelled, so IsCancelled() stays false), and
//     otherwise set to now + period: no catch-up after a long frame.
//   - NewTimer / NewTicker return the callback's container, so a container
//     passed in comes back equal to itself, and Cancel on either stops the
//     timer and anything else that holds it.
//   - Owners (Timer.h): a ModelScene's timers share the queue and the
//     handles, with deadlines on the scene's clock; the scene fires its own,
//     the WorldTick only C_Timer's.
//   - Not ported: LimitedLuaResources' "Timer" cap (FUN_141b723c0), which only
//     binds tainted code.

namespace {

using Event::FunctionContainer::Container;

constexpr double kMaxSeconds = 4294967.295;

struct Entry {
    int id;
    const void *owner;   // null = C_Timer; else the object whose clock this is on
    uint64_t deadline;   // ms on the owner's clock
    uint32_t period;     // ms; a ticker's interval
    bool ticker;         // NewTimer / NewTicker; else After
    bool zero;           // a 0 ms duration
    bool armed;          // a zero timer that has come due: due every pass from here on
    bool counted;        // iterations given (NewTimer: 1)
    uint32_t remaining;  // fires left after the next one, while counted
    bool done;
    Container *callback; // one reference, owned by the entry
    // The addon profiler's owner as the timer was made; its callback runs
    // inside a Scope for it.
    Profile::AddOnProfiler::Record *profileOwner;
};

std::vector<Entry> g_timers;
int g_nextID = 1;

// The earliest moment a live C_Timer timer can be due: a pass before it has
// nothing to fire and returns at once. Scheduling lowers it; the sweep at the
// end of a firing pass recomputes it.
uint64_t g_nextDue = UINT64_MAX;

struct Due {
    bool zero;
    uint64_t deadline;
    int id;
};
std::vector<Due> g_due; // kept for its capacity

// ----- C_Timer's clock ---------------------------------------------------

uint64_t g_nowMs = 0;
uint32_t g_lastTick = 0;
bool g_clockStarted = false;

// The engine's ms tick summed from its wrap-safe steps.
uint64_t Now() {
    const uint32_t tick = Time::Clock::NowMs();
    if (g_clockStarted)
        g_nowMs += Time::Clock::Elapsed(g_lastTick, tick);
    g_clockStarted = true;
    g_lastTick = tick;
    return g_nowMs;
}

// ----- Queue ---------------------------------------------------------------

Entry *FindByID(int id) {
    for (auto &t : g_timers) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

// The first moment `t` can be due.
uint64_t FirstDue(const Entry &t) {
    if (t.zero)
        return t.armed ? 0 : t.deadline + 1;
    return t.deadline;
}

bool IsDue(const Entry &t, uint64_t now) { return !t.done && now >= FirstDue(t); }

void FireDue(const void *owner, uint64_t now) {
    if (g_timers.empty())
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;

    // Pass 1: the owner's due timers, every-frame ones first, then by
    // deadline. Timers made while these fire wait for a later pass.
    g_due.clear();
    for (const auto &t : g_timers) {
        if (t.owner == owner && IsDue(t, now))
            g_due.push_back({t.zero, t.deadline, t.id});
    }
    std::sort(g_due.begin(), g_due.end(), [](const Due &a, const Due &b) {
        if (a.zero != b.zero)
            return a.zero;
        if (!a.zero && a.deadline != b.deadline)
            return a.deadline < b.deadline;
        return a.id < b.id;
    });

    // Pass 2: fire each. Re-find it each time, as a callback may have made or
    // dropped timers.
    for (const Due &due : g_due) {
        Entry *t = FindByID(due.id);
        if (t == nullptr || t->done)
            continue;
        if (t->zero)
            t->armed = true;
        Container *c = t->callback;
        const bool ticker = t->ticker;
        Event::FunctionContainer::Retain(c); // a callback may drop the entry
        {
            const int top = Game::Lua::GetTop(L);
            const int errfunc = Game::Lua::PushErrorHandler(L);
            if (Event::FunctionContainer::PushFunction(L, c)) {
                int nargs = 0;
                if (ticker) {
                    Event::FunctionContainer::Push(L, c);
                    nargs = 1;
                }
                const Profile::AddOnProfiler::Scope profile(t->profileOwner);
                Game::Lua::PCall(L, nargs, 0, errfunc);
            }
            Game::Lua::SetTop(L, top);
        }

        t = FindByID(due.id);
        if (t != nullptr) {
            if (!ticker || Event::FunctionContainer::IsCancelled(c) || (t->counted && t->remaining == 0)) {
                t->done = true;
            } else {
                if (t->counted)
                    --t->remaining;
                t->deadline = now + t->period;
            }
        }
        Event::FunctionContainer::Release(c);
    }

    // Pass 3: drop finished timers and those whose container was cancelled,
    // in one compacting pass, and find the next C_Timer moment among the rest.
    size_t keep = 0;
    g_nextDue = UINT64_MAX;
    for (const Entry &t : g_timers) {
        if (t.done || Event::FunctionContainer::IsCancelled(t.callback)) {
            Event::FunctionContainer::Release(t.callback);
            continue;
        }
        if (t.owner == nullptr)
            g_nextDue = (std::min)(g_nextDue, FirstDue(t));
        g_timers[keep++] = t;
    }
    g_timers.resize(keep);
}

void Tick() {
    const uint64_t now = Now();
    if (now < g_nextDue)
        return;
    FireDue(nullptr, now);
}

void Schedule(Container *c, const void *owner, uint64_t now, uint32_t durationMs, bool ticker, bool counted,
              uint32_t remaining) {
    Entry e{};
    e.id = g_nextID++;
    e.owner = owner;
    e.deadline = now + durationMs;
    e.period = durationMs;
    e.ticker = ticker;
    e.zero = durationMs == 0;
    e.counted = counted;
    e.remaining = remaining;
    e.callback = c;
    e.profileOwner = Profile::AddOnProfiler::Owner();
    g_timers.push_back(e);
    if (owner == nullptr)
        g_nextDue = (std::min)(g_nextDue, FirstDue(e));
}

// ----- Lua surface -------------------------------------------------------

int __fastcall Script_C_Timer_After(void *L) {
    return Script(L, 1, nullptr, Now(), Kind::After, "Usage: C_Timer.After(seconds, callback)",
                  "C_Timer.After");
}

int __fastcall Script_C_Timer_NewTimer(void *L) {
    return Script(L, 1, nullptr, Now(), Kind::NewTimer,
                  "Usage: local cbObject = C_Timer.NewTimer(seconds, callback)", "C_Timer.NewTicker");
}

int __fastcall Script_C_Timer_NewTicker(void *L) {
    return Script(L, 1, nullptr, Now(), Kind::NewTicker,
                  "Usage: local cbObject = C_Timer.NewTicker(seconds, callback [, iterations])",
                  "C_Timer.NewTicker");
}

const Game::Doc::Field kAfterArgs[] = {
    Game::Doc::Req("seconds", "number", "0 fires on the next frame."),
    Game::Doc::Req("callback", "function",
                   "A function, or a container from C_FunctionContainers.CreateCallback. Called with "
                   "no arguments."),
};
const Game::Doc::Function kAfter{"Calls a function once after a delay.", kAfterArgs, {}};

const Game::Doc::Field kNewTimerArgs[] = {
    Game::Doc::Req("seconds", "number"),
    Game::Doc::Req("callback", "function",
                   "A function, or a container from C_FunctionContainers.CreateCallback. Called with "
                   "cbObject."),
};
const Game::Doc::Field kHandleRets[] = {
    Game::Doc::Req("cbObject", "LuaFunctionContainer",
                   "The container that holds the callback: the one passed in, or a new one for a "
                   "function. Cancel() stops the timer."),
};
const Game::Doc::Function kNewTimer{"Calls a function once after a delay and returns its callback "
                                    "container, which can cancel it.",
                                    kNewTimerArgs, kHandleRets};

const Game::Doc::Field kNewTickerArgs[] = {
    Game::Doc::Req("seconds", "number", "The period."),
    Game::Doc::Req("callback", "function",
                   "A function, or a container from C_FunctionContainers.CreateCallback. Called with "
                   "cbObject."),
    Game::Doc::Opt("iterations", "number", nullptr,
                   "How many times to call it. Without it, the ticker runs until it is cancelled."),
};
const Game::Doc::Function kNewTicker{"Calls a function repeatedly at an interval and returns its "
                                     "callback container, which can cancel it.",
                                     kNewTickerArgs, kHandleRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Timer", "After", &Script_C_Timer_After, &kAfter);
    Game::Lua::RegisterTableFunction("C_Timer", "NewTimer", &Script_C_Timer_NewTimer,
                                     &kNewTimer);
    Game::Lua::RegisterTableFunction("C_Timer", "NewTicker", &Script_C_Timer_NewTicker,
                                     &kNewTicker);
}

} // namespace

int Script(void *L, int first, const void *owner, uint64_t nowMs, Kind kind, const char *usage,
           const char *api) {
    namespace Arg = Game::Lua::Arg;
    double seconds = 0.0;
    Arg::FiniteNumber(L, first, usage, seconds);
    if (!Event::FunctionContainer::IsCallbackArg(L, first + 1)) {
        Arg::Usage(L, first + 1, usage);
        return 0;
    }
    bool counted = kind == Kind::NewTimer;
    uint32_t iterations = 1;
    if (kind == Kind::NewTicker) {
        const int type = Game::Lua::Type(L, first + 2);
        if (type != Game::Lua::TYPE_NONE && type != Game::Lua::TYPE_NIL) {
            Arg::UInt32(L, first + 2, usage, iterations);
            counted = true;
        }
    }
    if (seconds < 0.0 || seconds > kMaxSeconds) {
        static constexpr const char *kFn[] = {"After", "NewTimer", "NewTicker"};
        char text[Arg::kErrorCap + 1];
        std::snprintf(text, sizeof(text), "%s(): %s %s", kFn[static_cast<int>(kind)], api,
                      seconds < 0.0 ? "requires a non-negative duration" : "duration is too large");
        Arg::RaiseMessage(L, text);
        return 0;
    }

    Container *c = Event::FunctionContainer::FromCallbackArg(L, first + 1);
    // The count after the first fire: 1 iteration is 0 more, and 0 wraps to
    // 0xFFFFFFFF, practically forever, as 3.80.2's stored iterations - 1 does.
    Schedule(c, owner, nowMs, static_cast<uint32_t>(seconds * 1000.0), kind != Kind::After, counted,
             iterations - 1);
    if (kind == Kind::After)
        return 0;
    Event::FunctionContainer::Push(L, c);
    return 1;
}

void Fire(const void *owner, uint64_t nowMs) { FireDue(owner, nowMs); }

void CancelOwned(const void *owner) {
    size_t keep = 0;
    for (const Entry &t : g_timers) {
        if (t.owner == owner) {
            Event::FunctionContainer::Release(t.callback);
            continue;
        }
        g_timers[keep++] = t;
    }
    g_timers.resize(keep);
}

// A /reload (or logout) resets the Lua state: drop every timer, as their
// callbacks die with it.
void PrepareForReload() {
    for (const Entry &t : g_timers)
        Event::FunctionContainer::Release(t.callback);
    g_timers.clear();
    g_nextID = 1;
    g_nextDue = UINT64_MAX;
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
static const Tick::WorldTick::AutoSubscribe _tick{&Tick};

} // namespace Time::Timer
