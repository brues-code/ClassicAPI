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

// Script profiling — 3.3.5's per-closure `Profile` records, kept through
// Lua's own call/return hooks. See Cpu.cpp.

namespace Profile::Cpu {

// True for the session when `scriptProfile` was 1 as the Lua state started.
bool Enabled();

// Milliseconds in `ticks` of the profiling clock.
double Ms(int64_t ticks);

// The profiling clock (QueryPerformanceCounter).
int64_t Now();

// 3.3.5's lua_gcgetfunctiontime: a closure's own and inclusive time (in
// clock ticks) and its call count; all 0 when profiling is off or the
// closure never ran.
void FunctionTime(const void *closure, int64_t *time, int64_t *totaltime, uint32_t *calls);

// Coroutine bookkeeping, called by `coroutine.*`: a new thread needs the
// hook (threads do not inherit it), and a suspended thread's time is not
// charged to the functions waiting in it.
void OnThreadCreated(void *thread);
void OnResume(void *thread);
void OnYield(void *thread);

} // namespace Profile::Cpu
