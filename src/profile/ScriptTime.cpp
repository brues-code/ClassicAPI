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

// `GetScriptCPUUsage()` and `GetEventCPUUsage([event])`.
//
// 3.3.5's FrameScript_Execute — the one function that runs a script — times
// each run while profiling is on and adds it to `s_scriptTimeUsed`, and, for
// an event dispatch, to that event's `m_timeUsed`; every signal raises the
// event's `m_signalCount`, profiling or not. A nested run is timed on its own
// as well as inside its parent, as there. `GetEventCPUUsage` sums every
// event's time and count whatever name it is given (3.3.5's
// FrameScript_GetEventCPUUsage never reads it, and retail answers the same
// for a name as for none), so totals are all that is kept.
//
// Frame::ScriptArgs observes every handler run. An event dispatch is a run
// of the frame's OnEvent slot — the dispatchers are the only callers of that
// slot. Signals are counted at each dispatcher: the engine's no-arg one
// through Event::SignalHook, its variadic FUN_FIRE_EVENT at its inner
// get-context call (only reached when the event has a listener), and
// Event::Custom's own.

#include "profile/ScriptTime.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/SignalHook.h"
#include "frame/ScriptArgs.h"
#include "profile/Cpu.h"

#include <cstdint>
#include <vector>

namespace Profile::ScriptTime {

namespace {

int64_t g_scriptTime = 0;
int64_t g_eventTime = 0;
uint32_t g_signals = 0;

// Start times of the runs in progress, innermost last.
std::vector<int64_t> g_starts;

void OnRun(bool begin, int handlerRef, void *frame) {
    if (!Cpu::Enabled())
        return;
    if (begin) {
        g_starts.push_back(Cpu::Now());
        return;
    }
    if (g_starts.empty())
        return;
    const int64_t elapsed = Cpu::Now() - g_starts.back();
    g_starts.pop_back();
    g_scriptTime += elapsed;
    if (frame != nullptr &&
        Game::Read<int>(frame, Offsets::OFF_FRAME_ONEVENT_SLOT) == handlerRef)
        g_eventTime += elapsed;
}

const Frame::ScriptArgs::AutoObserve _run{&OnRun};

bool OnNoArgSignal(int /*eventID*/) {
    CountSignal();
    return false;
}

const Event::SignalHook::AutoSubscribe _noArgSignal{&OnNoArgSignal};

// Stands in for FUN_FIRE_EVENT's `call FUN_FRAMESCRIPT_GET_CONTEXT`.
void *GetContextCounted() {
    CountSignal();
    return Game::Read<void *>(Offsets::VAR_LUA_STATE);
}

int __fastcall Script_GetScriptCPUUsage(void *L) {
    Game::Lua::PushNumber(L, Cpu::Ms(g_scriptTime));
    return 1;
}

int __fastcall Script_GetEventCPUUsage(void *L) {
    Game::Lua::PushNumber(L, Cpu::Ms(g_eventTime));
    Game::Lua::PushNumber(L, static_cast<double>(g_signals));
    return 2;
}

const Game::Doc::Field kScriptRets[] = {
    Game::Doc::Req("usage", "number", "Milliseconds frame scripts have run."),
};
const Game::Doc::Function kGetScriptCPUUsage{
    "Returns the time frame scripts have run, while script profiling is on.",
    {}, kScriptRets, "ProfilingGlobals"};

const Game::Doc::Field kEventArgs[] = {
    Game::Doc::Opt("event", "cstring", nullptr, "Ignored; the totals cover every event."),
};
const Game::Doc::Field kEventRets[] = {
    Game::Doc::Req("usage", "number",
                   "Milliseconds event handlers have run, while script profiling is on."),
    Game::Doc::Req("numEvents", "number", "Events fired."),
};
const Game::Doc::Function kGetEventCPUUsage{
    "Returns the time event handlers have run and how many events fired.",
    kEventArgs, kEventRets, "ProfilingGlobals"};

void PatchCode() {
    Common::RedirectCall(Offsets::CALL_FIRE_EVENT_GET_CONTEXT,
                         Offsets::FUN_FRAMESCRIPT_GET_CONTEXT,
                         reinterpret_cast<const void *>(&GetContextCounted));
}

} // namespace

void CountSignal() { ++g_signals; }

void Reset() {
    g_scriptTime = 0;
    g_eventTime = 0;
    g_signals = 0;
}

static void RegisterLuaFunctions() {
    // A new state starts from zero, as 3.3.5's FrameScript_Initialize does.
    Reset();
    g_starts.clear();
    PatchCode();
    Game::Lua::RegisterGlobalFunction("GetScriptCPUUsage", &Script_GetScriptCPUUsage,
                                      &kGetScriptCPUUsage);
    Game::Lua::RegisterGlobalFunction("GetEventCPUUsage", &Script_GetEventCPUUsage,
                                      &kGetEventCPUUsage);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Profile::ScriptTime
