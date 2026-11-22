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

#include "cache/QueryLoad.h"

#include "Game.h"
#include "Offsets.h"
#include "cache/Query.h"
#include "cache/Record.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"

#include <ankerl/unordered_dense.h>

#include <cstdint>
#include <deque>
#include <vector>

namespace Cache::QueryLoad {

namespace {

// The order ids go out in. Priority ids (a client's PriorityFn) go first, a
// few a tick. Requests (the C_*.RequestLoad* functions) and then warm-ups
// share the background rate, requests first, so an id a caller asked for by
// name does not wait behind an addon's bulk scan.
enum Lane : uint8_t { LANE_PRIORITY, LANE_REQUEST, LANE_WARM, LANE_COUNT };

struct Pending {
    uint32_t sentMs; // when the last query went out (valid while issued)
    int timeouts;    // sends that went unanswered for kResendMs in the world
    int requests;    // requesting Track calls (see Track)
    bool implicit;   // a warm-up waits on it too
    bool issued;     // a query is in flight
    bool sentOnce;   // our callback is on the entry; resend with Cache::Query
    Lane lane;       // the lane it waits in while not issued
};

struct InFlight {
    uint32_t id;
    uint32_t sentMs;
};

struct Answer {
    uint32_t id;
    bool success;
};

// A query unanswered this long in the world is sent again, up to
// kMaxTimeouts times, then the id fails. The server answers a query or
// refuses it; only a dropped one goes silent, so the timeout only has to
// outlast a slow round trip.
constexpr uint32_t kResendMs = 5000;
constexpr int kMaxTimeouts = 3;
// Wall-clock delay past each world entry before queries go out. VAR_IN_WORLD
// (set by the enter-world initializer FUN_004908C0, cleared at the start of
// every transition) rises when the client has loaded, which can be before
// the server has put the player back in the world (a transfer completes on
// the server's receipt of the client's world-port ack). Queries sent in that
// gap are dropped; waiting keeps them out of it, and the resend recovers any
// that still land there. Also latched the instant one of our queries is
// answered in the world (an answer proves the server answers), in
// `OnEntryAnswer`.
constexpr uint32_t kSettleMs = 1000;
// Minimum spacing between background queries: 20 per second at most,
// wall-clock so the rate does not follow the frame rate.
constexpr uint32_t kMinRequestSpacingMs = 50;
// Priority ids per tick, with no spacing floor. The priority set is small
// (the player's carried items), so this resolves it fast without a burst:
// at most kPriorityPerTick + 1 packets a frame.
constexpr int kPriorityPerTick = 6;

// The owning modules' callbacks, one per kind. `Client` constructors in other
// translation units set them during dynamic initialization, in an unspecified
// order against this file's. So they stay out of `Slot`, whose containers make
// `g_slots` dynamically initialized as well: its initializer could otherwise
// run after a `Client` and reset the callbacks to null. This array has no
// initializer, so it is zero-filled before any dynamic initialization.
struct Callbacks {
    DoneFn done;
    PriorityFn priority;
};
Callbacks g_callbacks[KIND_COUNT];

// Per cache. Nothing here is walked whole: a tick looks only at the fronts of
// the lanes and of the in-flight list, and an answer finds its id in the map.
// So the cost of a tick does not depend on how many ids wait, and the set has
// no cap (3.80.2's has none, 0x14050E2D0). The server still sees at most the
// background rate.
//
// The lanes and the in-flight list may hold stale ids (completed, moved to
// another lane, or re-sent); each pop checks the id against its map entry.
struct Slot {
    uintptr_t cache;
    uintptr_t getRecord;
    const Callbacks *callbacks;
    ankerl::unordered_dense::map<uint32_t, Pending> pending;
    std::deque<uint32_t> lanes[LANE_COUNT];
    std::deque<InFlight> inFlight; // send order, so oldest first
    std::vector<Answer> answers;   // queued by the entry callback, reported by the tick
};

Slot g_slots[KIND_COUNT] = {
    {Offsets::VAR_ITEMDB_CACHE, Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD,
     &g_callbacks[KIND_ITEM]},
    {Offsets::VAR_CREATURE_CACHE, Offsets::FUN_CREATURE_GET_RECORD,
     &g_callbacks[KIND_CREATURE]},
    {Offsets::VAR_GAMEOBJECT_CACHE, Offsets::FUN_GAMEOBJECT_GET_RECORD,
     &g_callbacks[KIND_GAMEOBJECT]},
};

// Queries may go out: in the world, and settled since the last entry.
bool g_settled = false;
// When the world went live (VAR_IN_WORLD set); 0 = not in the world.
uint32_t g_inWorldSinceMs = 0;
uint32_t g_lastRequestMs = 0;
// The last tick was in the world: leaving it re-queues what is in flight.
bool g_wasInWorld = false;

bool InWorld() {
    return *reinterpret_cast<const volatile uint8_t *>(
               static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) != 0;
}

bool Loaded(const Slot &s, uint32_t id) {
    return Cache::Record::Peek(s.getRecord, s.cache, id) != nullptr;
}

Lane LaneFor(const Slot &s, uint32_t id, const Pending &p) {
    const PriorityFn priority = s.callbacks->priority;
    if (priority != nullptr && priority(id))
        return LANE_PRIORITY;
    return p.requests > 0 ? LANE_REQUEST : LANE_WARM;
}

void Enqueue(Slot &s, uint32_t id, Pending &p, bool front) {
    p.lane = LaneFor(s, id, p);
    if (front)
        s.lanes[p.lane].push_front(id);
    else
        s.lanes[p.lane].push_back(id);
}

// A waiting id moves to a better lane when it now has one: it became a
// priority id, or a warm-up was requested. The copy left in the old lane is
// skipped when popped.
void Promote(Slot &s, uint32_t id, Pending &p) {
    if (p.issued || p.lane == LANE_PRIORITY)
        return;
    const Lane lane = LaneFor(s, id, p);
    if (lane < p.lane) {
        p.lane = lane;
        s.lanes[lane].push_back(id);
    }
}

// Removes the id and reports it. `done` may run Lua that tracks more ids,
// which can move map entries, so nothing holds a reference across it.
void Complete(Slot &s, uint32_t id, bool success) {
    const auto it = s.pending.find(id);
    if (it == s.pending.end())
        return;
    const Pending p = it->second;
    s.pending.erase(it);
    if (s.callbacks->done != nullptr)
        s.callbacks->done(id, success, p.requests, p.implicit);
}

// The callback our first query puts on the engine's cache entry, one per kind.
// Every response parser (item 0x0055BDB0, creature 0x00556E20, game object
// 0x005588E0) runs an entry's callbacks for each record it reads: with
// success 1 after filling the entry and setting its valid byte, with 0 on a
// refusal (the id with its high bit set) before deleting the entry. The
// answer is only queued here, inside the engine's callback walk and the
// packet handler; the world tick reports it at the end of the frame
// (`ReportAnswers`), so no parser needs a hook of ours.
template <int K> void __stdcall OnEntryAnswer(void *userData, int success) {
    // One behind a loading screen was answered before the transfer, which
    // proves nothing about now.
    if (InWorld())
        g_settled = true;
    g_slots[K].answers.push_back(
        Answer{static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData)), success != 0});
}

using EntryCallbackFn = void(__stdcall *)(void *, int);
const EntryCallbackFn kEntryCallback[KIND_COUNT] = {
    &OnEntryAnswer<KIND_ITEM>, &OnEntryAnswer<KIND_CREATURE>, &OnEntryAnswer<KIND_GAMEOBJECT>};

void Send(int kind, uint32_t id, uint32_t now) {
    Slot &s = g_slots[kind];
    const auto it = s.pending.find(id);
    if (it == s.pending.end())
        return;
    Pending &p = it->second;
    if (p.sentOnce) {
        Cache::Query::Send(reinterpret_cast<void *>(s.cache), id);
    } else {
        // A new entry sends the query; an entry someone else created gets our
        // callback and no packet. With dedup, a repeat does not queue it twice.
        const uint64_t none = 0;
        Cache::Record::Get(s.getRecord, s.cache, id, &none,
                           reinterpret_cast<void *>(kEntryCallback[kind]),
                           reinterpret_cast<void *>(static_cast<uintptr_t>(id)), true);
    }
    p.issued = true;
    p.sentOnce = true;
    p.sentMs = now;
    s.inFlight.push_back(InFlight{id, now});
}

// The next id in `lane` that still waits there and is not cached yet, or 0.
// An id the cache filled some other way while it waited (the engine's own
// query, a server push) completes here without a query.
uint32_t PopSendable(Slot &s, Lane lane) {
    std::deque<uint32_t> &queue = s.lanes[lane];
    while (!queue.empty()) {
        const uint32_t id = queue.front();
        queue.pop_front();
        const auto it = s.pending.find(id);
        if (it == s.pending.end() || it->second.issued || it->second.lane != lane)
            continue;
        if (Loaded(s, id)) {
            Complete(s, id, true);
            continue;
        }
        return id;
    }
    return 0;
}

// Out of the world the server may have dropped every query in flight, so
// each goes back to the front of its lane to be sent again.
void RequeueInFlight() {
    for (Slot &s : g_slots) {
        while (!s.inFlight.empty()) {
            const InFlight f = s.inFlight.back();
            s.inFlight.pop_back();
            const auto it = s.pending.find(f.id);
            if (it == s.pending.end() || !it->second.issued || it->second.sentMs != f.sentMs)
                continue;
            it->second.issued = false;
            Enqueue(s, f.id, it->second, /*front=*/true);
        }
    }
}

// A query unanswered for kResendMs goes again, at the front of its lane; the
// id fails after kMaxTimeouts. The list is in send order, so only its front
// can be due.
void ExpireInFlight(Slot &s, uint32_t now) {
    while (!s.inFlight.empty()) {
        const InFlight f = s.inFlight.front();
        const auto it = s.pending.find(f.id);
        if (it == s.pending.end() || !it->second.issued || it->second.sentMs != f.sentMs) {
            s.inFlight.pop_front();
            continue;
        }
        if (Time::Clock::Elapsed(f.sentMs, now) < kResendMs)
            return;
        s.inFlight.pop_front();
        // A record the cache holds is never failed or sent again, however it
        // arrived.
        if (Loaded(s, f.id)) {
            Complete(s, f.id, true);
            continue;
        }
        if (++it->second.timeouts >= kMaxTimeouts) {
            Complete(s, f.id, false);
            continue;
        }
        it->second.issued = false;
        Enqueue(s, f.id, it->second, /*front=*/true);
    }
}

// Reports the answers the entry callbacks queued since the last tick. Their
// records are filled (the valid byte is set before the callback walk), and
// this runs at the frame's end, where Lua is safe.
void ReportAnswers() {
    for (Slot &s : g_slots) {
        // `done` may run Lua; read by index, so an answer added meanwhile is
        // still reported, then clear the list.
        for (std::size_t i = 0; i < s.answers.size(); ++i) {
            const Answer a = s.answers[i];
            Complete(s, a.id, a.success);
        }
        s.answers.clear();
    }
}

// The one place any query goes out, so the rate floor governs every path.
//   1. Out of the world nothing is sent, and every query in flight is queued
//      to go again, since the server may have dropped it. Back in the world,
//      queries wait kSettleMs (or the first answer).
//   2. Priority ids go out first, kPriorityPerTick a tick; requests and then
//      warm-ups are background, one per kMinRequestSpacingMs across all
//      caches.
//   3. A query unanswered for kResendMs goes again, and the id fails after
//      kMaxTimeouts.
void OnWorldTick() {
    const uint32_t now = Time::Clock::NowMs();
    ReportAnswers();
    if (!InWorld()) {
        if (g_wasInWorld)
            RequeueInFlight();
        g_wasInWorld = false;
        g_settled = false;
        g_inWorldSinceMs = 0;
        return;
    }
    g_wasInWorld = true;
    if (!g_settled) {
        if (g_inWorldSinceMs == 0)
            g_inWorldSinceMs = now;
        if (Time::Clock::Elapsed(g_inWorldSinceMs, now) >= kSettleMs)
            g_settled = true;
    }

    for (Slot &s : g_slots)
        ExpireInFlight(s, now);
    if (!g_settled)
        return;

    int priorityBudget = kPriorityPerTick;
    for (int kind = 0; kind < KIND_COUNT && priorityBudget > 0; ++kind) {
        while (priorityBudget > 0) {
            const uint32_t id = PopSendable(g_slots[kind], LANE_PRIORITY);
            if (id == 0)
                break;
            Send(kind, id, now);
            --priorityBudget;
        }
    }

    if (Time::Clock::Elapsed(g_lastRequestMs, now) < kMinRequestSpacingMs)
        return;
    for (const Lane lane : {LANE_REQUEST, LANE_WARM}) {
        for (int kind = 0; kind < KIND_COUNT; ++kind) {
            const uint32_t id = PopSendable(g_slots[kind], lane);
            if (id != 0) {
                Send(kind, id, now);
                g_lastRequestMs = now;
                return;
            }
        }
    }
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

} // namespace

Client::Client(Kind kind, DoneFn done, PriorityFn priority) {
    g_callbacks[kind].done = done;
    g_callbacks[kind].priority = priority;
}

bool Track(Kind kind, uint32_t id, bool requested) {
    Slot &s = g_slots[kind];
    if (id == 0)
        return false;
    const auto it = s.pending.find(id);
    if (Loaded(s, id)) {
        // Filled some other way while it waited: report the waiters now.
        if (it != s.pending.end()) {
            if (requested)
                ++it->second.requests;
            Complete(s, id, true);
        } else if (requested && s.callbacks->done != nullptr) {
            s.callbacks->done(id, true, 1, false);
        }
        return true;
    }
    if (it != s.pending.end()) {
        Pending &p = it->second;
        if (requested)
            ++p.requests;
        else
            p.implicit = true;
        Promote(s, id, p);
        return true;
    }
    Pending p{};
    p.requests = requested ? 1 : 0;
    p.implicit = !requested;
    Pending &added = s.pending.emplace(id, p).first->second;
    Enqueue(s, id, added, /*front=*/false);
    return true;
}

void Prioritize(Kind kind, uint32_t id) {
    Slot &s = g_slots[kind];
    const auto it = s.pending.find(id);
    if (it != s.pending.end())
        Promote(s, id, it->second);
}

bool IsCached(Kind kind, uint32_t id) {
    return id != 0 && Loaded(g_slots[kind], id);
}

bool Settled() {
    return g_settled;
}

int PendingCount(Kind kind) {
    return static_cast<int>(g_slots[kind].pending.size());
}

} // namespace Cache::QueryLoad
