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

// `GetTimeCached()` — a frame-stable companion to vanilla `GetTime()`.
//
// 1.12's `GetTime()` is *live*: `Script_GetTime` (`0x00515ea0`) calls the
// OS tick source (`FUN_OS_TICKCOUNT_MS` → `GetTickCount`, or QPC in
// timing-mode 1) on every invocation and scales by 0.001, so two calls in
// the same frame can differ. Retail 4.x changed `GetTime()` to read a
// value sampled once per frame in the main loop, so all same-frame calls
// return the identical timestamp. Rather than change vanilla `GetTime`'s
// long-standing live behavior, we expose the frame-stable value under a
// new name and leave `GetTime()` alone.
//
// `GetTimeCached()` returns seconds on the SAME epoch as `GetTime()` (the
// OS millisecond counter × 0.001), so the two are directly comparable —
// the only difference is that `GetTimeCached()` holds constant for the
// duration of a frame. We refresh the cache from the shared once-per-frame
// `Tick::WorldTick` hook (which fires at the tail of each world frame).

#include "Game.h"
#include "Offsets.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"

#include <cstdint>

namespace Time::Cached {

namespace {

// OS millisecond counter sampled at the last frame boundary. 0 until the
// first world tick fires (pre-world). Read as uint32 — `GetTickCount`
// wraps at ~24.86 days, matching how the rest of the codebase treats this
// counter (see `Time::Clock`), and how `Script_GetTime` masks it to 32
// bits before scaling.
uint32_t g_frameMs = 0;

uint32_t SampleTickMs() { return Time::Clock::NowMs(); }

// Tail-of-frame refresh. Tail-of-frame-N ≈ start-of-frame-(N+1) minus the
// inter-frame gap, which is all a frame-stable timestamp needs: every read
// during a frame returns the value stamped at the previous boundary.
void OnWorldTick() {
    g_frameMs = SampleTickMs();
}

// `GetTimeCached()` → number (seconds). Frame-stable analog of
// `GetTime()`, same epoch. Falls back to a live sample when no frame has
// ticked yet (pre-world / glue), so it never returns 0.
int __fastcall Script_GetTimeCached(void *L) {
    uint32_t ms = g_frameMs;
    if (ms == 0)
        ms = SampleTickMs();
    Game::Lua::PushNumber(L, static_cast<double>(ms) * 0.001);
    return 1;
}

const Game::Doc::Field kRets[] = {
    Game::Doc::Req("time", "number", "Seconds, on the same clock as GetTime."),
};
const Game::Doc::Function kGetTimeCached{
    "Returns GetTime's value as of the start of the current frame, the same for every call in "
    "that frame.",
    {}, kRets, "SystemGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTimeCached", &Script_GetTimeCached, &kGetTimeCached);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Time::Cached
