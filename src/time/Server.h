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

namespace Time::Server {

// The client receives TWO different clocks, and they answer two different
// questions. Both accessors return 0 when their source has no data yet
// (before login). Pick by what the caller's contract says, not by
// convenience — on a realm that is not on UTC, or in a zone the realm time
// -shifts, these return very different values.

// The instant: a true Unix epoch, seconds since 1970-01-01 UTC. Backed by
// the engine's own `CMSG_QUERY_TIME` sync, so it has real seconds and is
// free of both the realm's timezone and any per-map time offset. Use it for
// anything that compares, sorts, stores, or counts down — `GetServerTime`
// and the daily reset.
//
// Falls back to `RealmClockEpoch` for the ~1 RTT after enter-world before
// the sync lands, and on a server that never answers the query. That
// fallback is the wrong clock by the realm's UTC offset, so a caller can
// see the value jump once during the first moments of a session.
int64_t CurrentEpoch();

// The realm's wall clock, expressed as the epoch that renders back to those
// same digits under UTC. Built from the gametime the server broadcasts, so
// it matches `GetGameTime()` exactly — including a per-map time offset where
// the realm applies one. Minute granularity, with `GetTickCount` filling the
// sub-minute part.
//
// This is not an instant and must not be compared against a real timestamp.
// Use it only to DISPLAY realm time: the calendar's date and time fields,
// and `GetServerTimeLocal`, whose contract is "the epoch offset by the
// server's timezone" — which is precisely this value.
int64_t RealmClockEpoch();

} // namespace Time::Server
