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

namespace Time::Timer {

// The C_Timer queue, shared with other clocks. Each timer has an owner:
// null for C_Timer (its own clock, fired from the WorldTick), or an object
// with a clock of its own, which fires its timers itself with Fire(owner,
// now) and drops them with CancelOwned(owner). Deadlines are in the owner's
// seconds. Handles and callbacks are C_Timer's.

enum class Kind { After, NewTimer, NewTicker };

// The body of C_Timer.After / NewTimer / NewTicker with the seconds at stack
// index `first` (the callback, then the ticker's iterations, after it), on
// `owner`'s clock at `now`. Raises `usage` on bad arguments. Returns the
// count of Lua results.
int Script(void *L, int first, const void *owner, double now, Kind kind, const char *usage);

// Runs `owner`'s timers that are due at `now`, in the order they were made.
void Fire(const void *owner, double now);

// Drops `owner`'s timers; their handles answer IsCancelled true.
void CancelOwned(const void *owner);

} // namespace Time::Timer
