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

// Loading records into the client query caches: the item, creature and game
// object caches. Every query goes out through one queue, so the same rules
// hold for all three.
//
// - World gate. The server drops a *_QUERY opcode (STATUS_LOGGEDIN) while
//   the player is not in the world: before login completes and behind every
//   loading screen. A cache sends its query only when it creates the pending
//   entry, so a dropped query leaves the entry pending for the session
//   (Cache::Query). Nothing goes out until the world has been live for a
//   settle delay, or an answer has arrived in it.
// - Resend. A query in flight across a loading screen, or unanswered for a
//   few seconds in the world, is sent again; after a few silent sends the id
//   fails.
// - Rate. One background query per 50 ms (issue #25: an unthrottled /reload
//   burst of 1,325 queries in 14 ms dropped a router's NAT table). A cache's
//   priority ids go out ahead of that floor, a few per tick.
// - Answer. Each cache's response parser is hooked. After the engine fills
//   the entry and runs its callbacks, the queue reports every tracked id the
//   cache now holds. A server refusal reaches the queue's own callback, the
//   one whose presence makes `_GetRecord` send (Cache::Record), and fails
//   the id at once.
namespace Cache::QueryLoad {

enum Kind { KIND_ITEM, KIND_CREATURE, KIND_GAMEOBJECT, KIND_COUNT };

// A tracked id resolved: `success` when the cache holds its record, else
// the server refused it or every send went unanswered. `requested` is true
// when any Track call for it was a request (see Track).
using DoneFn = void (*)(uint32_t id, bool success, bool requested);
// True for an id that goes out ahead of the background rate floor.
using PriorityFn = bool (*)(uint32_t id);

// The module that owns a cache's events. One file-scope `static const`
// instance per kind; `priority` may be null.
struct Client {
    Client(Kind kind, DoneFn done, PriorityFn priority = nullptr);
};

// Tracks `id` until it resolves. A `requested` load is one a caller asked for
// by name (the C_*.RequestLoad* functions): when the record is already
// cached, `done(id, true, true)` runs before this returns. A warm-up (not
// requested) of a cached record does nothing, the way the engine's own
// lookups report a hit to nobody. A request for an id a warm-up already
// tracks makes it requested. False only when the pending set is full.
bool Track(Kind kind, uint32_t id, bool requested);

// True when the cache holds `id`'s record. Never sends a query.
bool IsCached(Kind kind, uint32_t id);

// True once queries may go out: in the world, and settled since the last
// world entry.
bool Settled();

// The number of ids still tracked for `kind`.
int PendingCount(Kind kind);

} // namespace Cache::QueryLoad
