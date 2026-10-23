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

// Script profiling: the `scriptProfile` cvar, `GetFunctionCPUUsage`,
// `UpdateAddOnCPUUsage`, `GetAddOnCPUUsage` and `ResetCPUUsage`.
//
// 3.3.5 gives every closure a `Profile {calls, depth, start, time,
// totaltime}` when the state is created with profiling on, and keeps it from
// a second interpreter (`luaV_execute_profile` + `luaD_precall_profile`)
// that reads the clock at each call and return. 1.12 has no profile field
// and one interpreter, but Lua 5.0 calls its debug hook at exactly those
// points — CALL for every C and Lua call (tail calls included), RET at every
// return — and nothing else uses the hook. So with `scriptProfile` 1 the hook
// is set for the session and applies 3.3.5's arithmetic to records kept here,
// per closure, dropped when the closure is collected (`Gc::Hooks`):
//
//   a Lua function's `time` is its own running time: each call it makes ends
//     a segment (`time += now - last`) and its return ends the last one;
//   `depth` counts the calls it has outstanding — when it goes 0 → 1 the
//     span starts (`start`), and when the call returns to depth 0 the span is
//     added to `totaltime`, which also takes the segments run at depth 0 — so
//     `totaltime` is the inclusive time, recursion counted once;
//   a C function's `time` and `totaltime` both take its whole call;
//   `calls` counts every call, a tail call included.
//
// The hook keeps a shadow of each thread's call stack (closure, call record
// index, tail-call count) to know the caller of each event. Two things leave
// that stack without a RET event, and both are recognized from the record
// index of the next event: a tail call (OP_TAILCALL replaces the record and
// raises its tail-call count), where the replaced function ends its segment as
// 3.3.5's OP_TAILCALL does; and an error unwinding frames, where each removed
// frame that had a call outstanding loses that depth without any time, as
// 3.3.5's luaD_pcall error path does.
//
// The hook is installed only with `scriptProfile` 1, read as each state starts
// (3.3.5 creates the state with it): with profiling off nothing runs.

#include "profile/Cpu.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/Registry.h"
#include "cvar/Factory.h"
#include "gc/Hooks.h"
#include "gc/Tag.h"
#include "profile/ScriptTime.h"

#include <ankerl/unordered_dense.h>
#include <windows.h>

#include <memory>
#include <vector>

namespace Profile::Cpu {

namespace {

using SetHook_t = int(__fastcall *)(void *L, void *func, int mask, int count);

struct Record {
    uint32_t calls = 0;
    int depth = 0;
    int64_t start = 0;
    int64_t time = 0;
    int64_t totaltime = 0;
};

struct Frame {
    const void *closure;
    int ci;
    uint32_t tailcalls;
    bool isC;
    bool done; // a C frame whose time was settled at a yield
    int64_t last;
    int64_t callStart;
};

using Stack = std::vector<Frame>;

bool g_enabled = false;
bool g_hooked = false; // the hook was set on the current main state
double g_msPerTick = 0.0;
CVar::Factory::Handle g_cvar = nullptr;

ankerl::unordered_dense::map<const void *, Record> g_records;
ankerl::unordered_dense::map<const void *, std::unique_ptr<Stack>> g_stacks;
ankerl::unordered_dense::map<const char *, int64_t> g_addonTime;

// The last thread an event came from, so a run of events on one thread skips
// the map lookup. Reset whenever g_stacks changes shape.
const void *g_lastThread = nullptr;
Stack *g_lastStack = nullptr;

Stack &StackOf(const void *L) {
    if (L == g_lastThread)
        return *g_lastStack;
    auto &slot = g_stacks[L];
    if (!slot)
        slot = std::make_unique<Stack>();
    g_lastThread = L;
    g_lastStack = slot.get();
    return *slot;
}

Record &RecordOf(const void *closure) { return g_records[closure]; }

// Removes frames at or above record `ci` that left without a return: an
// error unwound them. The topmost had no call outstanding; each below it had
// one, which is abandoned.
void Unwind(Stack &s, int ci) {
    bool topmost = true;
    while (!s.empty() && s.back().ci >= ci) {
        const Frame f = s.back();
        s.pop_back();
        if (!topmost && !f.isC)
            RecordOf(f.closure).depth--;
        topmost = false;
    }
}

void OnCall(void *L, Stack &s, int ci, int64_t now) {
    const uint8_t *info = Game::Read<const uint8_t *>(L, Offsets::OFF_LUA_STATE_BASE_CI) +
                          ci * Offsets::SIZEOF_LUA_CALLINFO;
    const uint8_t *func = Game::Read<const uint8_t *>(info, Offsets::OFF_LUA_CALLINFO_BASE) -
                          Offsets::SIZEOF_LUA_TVALUE;
    const void *closure = Game::Read<const void *>(func, Offsets::OFF_LUA_TVALUE_VALUE);
    const bool isC = Game::Read<uint8_t>(closure, Offsets::OFF_LUA_CLOSURE_ISC) != 0;
    const uint32_t tailcalls =
        isC ? 0 : Game::Read<uint32_t>(info, Offsets::OFF_LUA_CALLINFO_TAILCALLS);

    bool tailcall = false;
    if (!isC && !s.empty() && s.back().ci == ci && !s.back().isC &&
        tailcalls == s.back().tailcalls + 1) {
        // OP_TAILCALL: the replaced function's last segment.
        const Frame f = s.back();
        s.pop_back();
        Record &r = RecordOf(f.closure);
        r.time += now - f.last;
        if (r.depth == 0)
            r.totaltime += now - f.last;
        tailcall = true;
    }
    Unwind(s, ci);
    if (!tailcall && !s.empty() && !s.back().isC) {
        // OP_CALL: the caller's segment ends and its call is outstanding.
        Frame &caller = s.back();
        Record &r = RecordOf(caller.closure);
        r.time += now - caller.last;
        if (++r.depth == 1) {
            r.start = now;
            r.totaltime += now - caller.last;
        }
        caller.last = now;
    }
    RecordOf(closure).calls++;
    s.push_back({closure, ci, tailcalls, isC, false, now, now});
}

void OnReturn(Stack &s, int ci, int64_t now) {
    Unwind(s, ci + 1);
    if (s.empty() || s.back().ci != ci)
        return; // the call began before profiling did
    const Frame f = s.back();
    s.pop_back();
    Record &r = RecordOf(f.closure);
    if (f.isC) {
        if (!f.done) {
            r.time += now - f.callStart;
            r.totaltime += now - f.callStart;
        }
    } else {
        r.time += now - f.last;
        if (r.depth == 0)
            r.totaltime += now - f.last;
    }
    if (!s.empty() && !s.back().isC) {
        Frame &caller = s.back();
        Record &rc = RecordOf(caller.closure);
        if (rc.depth-- == 1)
            rc.totaltime += now - rc.start;
        caller.last = now;
    }
}

void __fastcall Hook(void *L, void *ar) {
    const int event = Game::Read<int>(ar, Offsets::OFF_LUA_DEBUG_EVENT);
    if (event != Offsets::LUA_HOOK_CALL && event != Offsets::LUA_HOOK_RET)
        return;
    const int64_t now = Now();
    const int ci = Game::Read<int>(ar, Offsets::OFF_LUA_DEBUG_I_CI);
    Stack &s = StackOf(L);
    if (event == Offsets::LUA_HOOK_CALL)
        OnCall(L, s, ci, now);
    else
        OnReturn(s, ci, now);
}

void SetHook(void *L, bool on) {
    reinterpret_cast<SetHook_t>(Offsets::LUA_SET_HOOK)(
        L, on ? reinterpret_cast<void *>(&Hook) : nullptr,
        on ? Offsets::LUA_MASK_CALL | Offsets::LUA_MASK_RET : 0, 0);
}

void OnFree(const void *object) {
    if (g_records.empty() && g_stacks.empty())
        return;
    switch (Game::Read<uint8_t>(object, Offsets::OFF_LUA_GCHEADER_TT)) {
    case Game::Lua::TYPE_FUNCTION:
        g_records.erase(object);
        break;
    case Game::Lua::TYPE_THREAD:
        if (g_stacks.erase(object) != 0) {
            g_lastThread = nullptr;
            g_lastStack = nullptr;
        }
        break;
    default:
        break;
    }
}

const Gc::Hooks::AutoSubscribe _gc{nullptr, &OnFree};

// --- Lua API ----------------------------------------------------------------

int __fastcall Script_GetFunctionCPUUsage(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "Usage: GetFunctionCPUUsage(function[, includeSubroutines])");
        return 0;
    }
    const bool includeSubroutines = Game::Lua::ToFlag(L, 2, true);
    int64_t time = 0;
    int64_t totaltime = 0;
    uint32_t calls = 0;
    FunctionTime(Game::Lua::ToPointer(L, 1), &time, &totaltime, &calls);
    Game::Lua::PushNumber(L, Ms(includeSubroutines ? totaltime : time));
    Game::Lua::PushNumber(L, static_cast<double>(calls));
    return 2;
}

// 3.3.5's luaC_timeall: each closure's own time, summed by the closure's tag.
int __fastcall Script_UpdateAddOnCPUUsage(void * /*L*/) {
    if (!g_enabled)
        return 0;
    for (auto &entry : g_addonTime)
        entry.second = 0;
    for (const auto &[closure, record] : g_records)
        if (const char *tag = Gc::Tag::Of(closure))
            g_addonTime[tag] += record.time;
    return 0;
}

int __fastcall Script_GetAddOnCPUUsage(void *L) {
    const uint8_t *record =
        Addons::ResolveRecord(L, "Usage: GetAddOnCPUUsage(index or \"name\")");
    double ms = 0.0;
    if (record != nullptr) {
        const auto it = g_addonTime.find(
            Game::Read<const char *>(record, Offsets::OFF_ADDON_ENTRY_NAME_PTR));
        if (it != g_addonTime.end())
            ms = Ms(it->second);
    }
    Game::Lua::PushNumber(L, ms);
    return 1;
}

// 3.3.5's FrameScript_ResetCPUUsage: the script and event totals, then
// luaC_timereset — calls and times, not the live depth/start.
int __fastcall Script_ResetCPUUsage(void * /*L*/) {
    ScriptTime::Reset();
    if (!g_enabled)
        return 0;
    for (auto &entry : g_addonTime)
        entry.second = 0;
    for (auto &entry : g_records) {
        entry.second.calls = 0;
        entry.second.time = 0;
        entry.second.totaltime = 0;
    }
    return 0;
}

const Game::Doc::Field kFunctionArgs[] = {
    Game::Doc::Req("func", "function"),
    Game::Doc::Opt("includeSubroutines", "bool", "true",
                   "Include the time spent in the functions it calls."),
};
const Game::Doc::Field kFunctionRets[] = {
    Game::Doc::Req("usage", "number", "Milliseconds spent in the function."),
    Game::Doc::Req("calls", "number", "Times the function was called."),
};
const Game::Doc::Function kGetFunctionCPUUsage{
    "Returns the time spent in a function and how often it was called, while script "
    "profiling is on.",
    kFunctionArgs, kFunctionRets, "ProfilingGlobals"};

const Game::Doc::Function kUpdateAddOnCPUUsage{
    "Recounts the time each addon's functions have run; GetAddOnCPUUsage returns the "
    "result of the last recount.",
    {}, {}, "ProfilingGlobals"};

const Game::Doc::Field kAddOnArgs[] = {
    Game::Doc::Req("name", "uiAddon", "An addon index or folder name."),
};
const Game::Doc::Field kAddOnRets[] = {
    Game::Doc::Req("usage", "number", "Milliseconds the addon's functions have run."),
};
const Game::Doc::Function kGetAddOnCPUUsage{
    "Returns the time an addon's functions have run, as of the last UpdateAddOnCPUUsage, "
    "while script profiling is on.",
    kAddOnArgs, kAddOnRets, "ProfilingGlobals"};

const Game::Doc::Function kResetCPUUsage{
    "Sets every script profiling count back to zero.", {}, {}, "ProfilingGlobals"};

} // namespace

bool Enabled() { return g_enabled; }

double Ms(int64_t ticks) { return static_cast<double>(ticks) * g_msPerTick; }

int64_t Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void FunctionTime(const void *closure, int64_t *time, int64_t *totaltime, uint32_t *calls) {
    *time = 0;
    *totaltime = 0;
    *calls = 0;
    if (!g_enabled)
        return;
    const auto it = g_records.find(closure);
    if (it == g_records.end())
        return;
    *time = it->second.time;
    *totaltime = it->second.totaltime;
    *calls = it->second.calls;
}

void OnThreadCreated(void *thread) {
    if (g_enabled)
        SetHook(thread, true);
}

void OnResume(void *thread) {
    if (!g_enabled)
        return;
    const int64_t now = Now();
    for (Frame &f : StackOf(thread))
        f.last = now;
}

void OnYield(void *thread) {
    if (!g_enabled)
        return;
    Stack &s = StackOf(thread);
    if (s.empty() || !s.back().isC || s.back().done)
        return;
    // The yield itself: its call ends here, not when the thread resumes.
    Frame &f = s.back();
    Record &r = RecordOf(f.closure);
    const int64_t elapsed = Now() - f.callStart;
    r.time += elapsed;
    r.totaltime += elapsed;
    f.done = true;
}

static void RegisterLuaFunctions() {
    g_cvar = CVar::Factory::Register("scriptProfile", "Whether or not script profiling is enabled",
                                     Offsets::CVAR_CATEGORY_DEBUG, "0");

    // A new state starts with nothing recorded, as 3.3.5's does.
    void *L = Game::Lua::State();
    if (g_hooked && L != nullptr)
        SetHook(L, false);
    g_records.clear();
    g_stacks.clear();
    g_addonTime.clear();
    g_lastThread = nullptr;
    g_lastStack = nullptr;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_msPerTick = 1000.0 / static_cast<double>(freq.QuadPart);
    g_enabled = CVar::Factory::GetInt(g_cvar) != 0;
    g_hooked = g_enabled && L != nullptr;
    if (g_hooked)
        SetHook(L, true);

    Game::Lua::RegisterGlobalFunction("GetFunctionCPUUsage", &Script_GetFunctionCPUUsage,
                                      &kGetFunctionCPUUsage);
    Game::Lua::RegisterGlobalFunction("UpdateAddOnCPUUsage", &Script_UpdateAddOnCPUUsage,
                                      &kUpdateAddOnCPUUsage);
    Game::Lua::RegisterGlobalFunction("GetAddOnCPUUsage", &Script_GetAddOnCPUUsage,
                                      &kGetAddOnCPUUsage);
    Game::Lua::RegisterGlobalFunction("ResetCPUUsage", &Script_ResetCPUUsage, &kResetCPUUsage);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Profile::Cpu
