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

// The engine's OS millisecond tick — the ONE place the codebase reads it.
//
// `FUN_OS_TICKCOUNT_MS` (→ `GetTickCount`, or a scaled rdtsc in timing-mode 1)
// returns an **unsigned** 32-bit millisecond counter on the SAME epoch that
// Lua's `GetTime()` scales by 0.001 (`Script_GetTime` masks the tick to the
// low 32 bits UNSIGNED, then × 0.001). Two hard consequences:
//
//   1. It is UNSIGNED and wraps at 2^32 ms (~49.7 days). Reading a tick into a
//      signed `int` makes every timestamp negative once the counter passes
//      2^31 ms (~24.9 days of uptime) — and the rdtsc backend can already be
//      past 2^31 right after a warm reboot. A negative `startTime + duration`
//      then lands in the past and callers see every cooldown/aura as already
//      elapsed. This exact bug shipped three times (cooldown by-ID getters,
//      LossOfControl); it must never come back.
//
//   2. Absolute ticks are only comparable through wrap-safe arithmetic. Never
//      write `now < deadline` on two raw ticks — it inverts for the ~24.9 days
//      after a wrap. Use `Reached()` / `Remaining()` below, which take the
//      signed difference (both operands wrap together, so the delta stays
//      correct as long as the interval is under 2^31 ms — always true here).
//
// RULE: read ticks only through `Time::Clock::NowMs()` (returns `uint32_t` — do
// not store it in a signed `int`), and compare them only through the helpers
// here. Push a tick to Lua as `(double)(uint32_t)tick` so it matches
// `GetTime()*1000` across the wrap (see `Spell::Cast::PushMs`).
namespace Time::Clock {

// Current engine ms tick. Unsigned by construction — assign to `uint32_t`.
uint32_t NowMs();

// The value Lua's `GetTime()` returns now: the tick as unsigned 32 bits, in
// seconds. For times Lua passes in as `GetTime()` seconds, which wrap with it.
inline double NowSeconds() { return static_cast<double>(NowMs()) * 0.001; }

// Wrap-safe "has `deadlineMs` arrived?" — true once `now` is at or past it.
inline bool Reached(uint32_t nowMs, uint32_t deadlineMs) {
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}
inline bool Reached(uint32_t deadlineMs) { return Reached(NowMs(), deadlineMs); }

// Wrap-safe ms elapsed since an earlier stamp (assumes < 2^31 ms elapsed).
inline uint32_t Elapsed(uint32_t sinceMs, uint32_t nowMs) { return nowMs - sinceMs; }
inline uint32_t Elapsed(uint32_t sinceMs) { return NowMs() - sinceMs; }

// Wrap-safe ms remaining until `deadlineMs`; 0 once it is reached/passed.
inline uint32_t Remaining(uint32_t nowMs, uint32_t deadlineMs) {
    return Reached(nowMs, deadlineMs) ? 0u : deadlineMs - nowMs;
}
inline uint32_t Remaining(uint32_t deadlineMs) { return Remaining(NowMs(), deadlineMs); }

} // namespace Time::Clock
