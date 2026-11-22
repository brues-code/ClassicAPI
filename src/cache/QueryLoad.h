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
//   priority ids go out ahead of that floor, a few per tick. Requests go
//   before warm-ups within the floor.
// - Answer. The first query for an id puts the queue's callback on the
//   engine's cache entry, the callback whose presence makes `_GetRecord` send
//   (Cache::Record). Each response parser runs it per record, success or
//   refusal; it only queues the answer, and the world tick reports it at the
//   end of the frame. An id the cache filled some other way while it waited
//   is reported when Track sees it again, when its turn to send comes, or
//   when its query times out.
// - Cost. No per-frame work depends on how many ids wait, so there is no cap
//   (issue #152: a walk of every waiting id each frame, under a quest
//   addon's login item scan, took a laptop from 60 FPS to 4).
namespace Cache::QueryLoad {

enum Kind { KIND_ITEM, KIND_CREATURE, KIND_GAMEOBJECT, KIND_COUNT };

// A tracked id resolved: `success` when the cache holds its record, else
// the server refused it or every send went unanswered. `requests` is the
// number of requesting Track calls made while it was pending, and `implicit`
// is true when a warm-up waited on it too (see Track).
using DoneFn = void (*)(uint32_t id, bool success, int requests, bool implicit);
// True for an id that goes out ahead of the background rate floor.
using PriorityFn = bool (*)(uint32_t id);

// The module that owns a cache's events. One file-scope `static const`
// instance per kind; `priority` may be null.
struct Client {
    Client(Kind kind, DoneFn done, PriorityFn priority = nullptr);
};

// Tracks `id` until it resolves. A `requested` load is one a caller asked for
// by name (the C_*.RequestLoad* functions): when the record is already
// cached, `done(id, true, 1, false)` runs before this returns. A warm-up (not
// requested) of a cached record does nothing, the way the engine's own
// lookups report a hit to nobody. While the id is pending, each request adds
// one to its count and a warm-up marks it implicit, so one answer reports
// every caller: the 3.80.2 client keeps one callback per request and one per
// id for its implicit readers (0x141382130, tag dedup 0x14045BDA0). The set
// has no cap, as 3.80.2's has none (0x14050E2D0). False only for id 0.
bool Track(Kind kind, uint32_t id, bool requested);

// Moves a waiting `id` to the priority lane when the kind's PriorityFn now
// says so. For a priority set that changes after its ids were queued (the
// player picks up an item a scan already queued). Does nothing for an id
// that is not waiting.
void Prioritize(Kind kind, uint32_t id);

// True when the cache holds `id`'s record. Never sends a query.
bool IsCached(Kind kind, uint32_t id);

// True once queries may go out: in the world, and settled since the last
// world entry.
bool Settled();

// The number of ids still tracked for `kind`.
int PendingCount(Kind kind);

} // namespace Cache::QueryLoad
