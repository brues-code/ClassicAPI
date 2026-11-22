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

#include <cstdint>

namespace Cache::QueryLoad {

namespace {

struct Pending {
    uint32_t id;
    uint32_t sentMs; // when the last query went out (valid while issued)
    int timeouts;    // sends that went unanswered for kResendMs in the world
    int requests;    // requesting Track calls (see Track)
    bool implicit;   // a warm-up waits on it too
    bool issued;     // a query is in flight
    bool sentOnce;   // our callback is on the entry; resend with Cache::Query
    bool failed;     // the server refused it (Escalated)
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
// that still land there. Also latched the instant an answer arrives in the
// world (an answer proves the server answers), in `OnResponse`.
constexpr uint32_t kSettleMs = 1000;
// Minimum spacing between background queries: 20 per second at most,
// wall-clock so the rate does not follow the frame rate.
constexpr uint32_t kMinRequestSpacingMs = 50;
// Priority ids per tick, with no spacing floor. The priority set is small
// (the player's carried items), so this resolves it fast without a burst:
// at most kPriorityPerTick + 1 packets a frame.
constexpr int kPriorityPerTick = 6;
// Ids waiting per cache. 3.80.2 has no cap, but this queue is not its
// loader: it drains 20 a second, and the world tick walks every entry each
// frame. A quest addon's item scan at login queues thousands of uncached
// ids, and without a cap that walk cost every frame until the queue drained
// (issue #152: 60 FPS to 4 on a laptop CPU).
constexpr int kMaxPending = 512;

struct Slot {
    uintptr_t cache;
    uintptr_t getRecord;
    DoneFn done;
    PriorityFn priority;
    Pending pending[kMaxPending];
    int count;
};

Slot g_slots[KIND_COUNT] = {
    {Offsets::VAR_ITEMDB_CACHE, Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD},
    {Offsets::VAR_CREATURE_CACHE, Offsets::FUN_CREATURE_GET_RECORD},
    {Offsets::VAR_GAMEOBJECT_CACHE, Offsets::FUN_GAMEOBJECT_GET_RECORD},
};

// Queries may go out: in the world, and settled since the last entry.
bool g_settled = false;
// When the world went live (VAR_IN_WORLD set); 0 = not in the world.
uint32_t g_inWorldSinceMs = 0;
uint32_t g_lastRequestMs = 0;

bool InWorld() {
    return *reinterpret_cast<const volatile uint8_t *>(
               static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) != 0;
}

bool Loaded(const Slot &s, uint32_t id) {
    return Cache::Record::Peek(s.getRecord, s.cache, id) != nullptr;
}

int Count(const Slot &s) {
    return s.count;
}

int Find(const Slot &s, uint32_t id) {
    for (int i = 0; i < Count(s); ++i)
        if (s.pending[i].id == id)
            return i;
    return -1;
}

void RemoveAt(Slot &s, int i) {
    --s.count;
    if (i != s.count)
        s.pending[i] = s.pending[s.count];
}

void Report(const Slot &s, const Pending &p, bool success) {
    if (s.done != nullptr)
        s.done(p.id, success, p.requests, p.implicit);
}

// Removes and reports the id at `i` if it resolved: loaded, or refused by the
// server. True if it did, so callers must not advance their index.
bool TryComplete(Slot &s, int i) {
    const Pending p = s.pending[i];
    bool success;
    if (Loaded(s, p.id))
        success = true;
    else if (p.failed)
        success = false;
    else
        return false;
    RemoveAt(s, i);
    Report(s, p, success);
    return true;
}

// The callback our first query carries, one per kind so a refusal finds its
// own cache. The engine calls it with the id we put in userData. A filled
// entry already reads as loaded, so success needs nothing. On a refusal (the
// id with its high bit set in the response) the engine leaves the entry
// unloaded (verified for the item cache in FUN_0055BDB0), so the id is marked
// here and fails on the next sweep.
template <int K> void __stdcall Escalated(void *userData, int success) {
    if (success)
        return;
    Slot &s = g_slots[K];
    const int i = Find(s, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData)));
    if (i >= 0)
        s.pending[i].failed = true;
}

using EscalatedFn = void(__stdcall *)(void *, int);
const EscalatedFn kEscalated[KIND_COUNT] = {&Escalated<KIND_ITEM>, &Escalated<KIND_CREATURE>,
                                            &Escalated<KIND_GAMEOBJECT>};

void Send(int kind, Pending &p, uint32_t now) {
    const Slot &s = g_slots[kind];
    if (p.sentOnce) {
        Cache::Query::Send(reinterpret_cast<void *>(s.cache), p.id);
    } else {
        // A new entry sends the query; an entry someone else created gets our
        // callback and no packet. With dedup, a repeat does not queue it twice.
        const uint64_t none = 0;
        Cache::Record::Get(s.getRecord, s.cache, p.id, &none,
                           reinterpret_cast<void *>(kEscalated[kind]),
                           reinterpret_cast<void *>(static_cast<uintptr_t>(p.id)), true);
    }
    p.issued = true;
    p.sentOnce = true;
    p.sentMs = now;
}

// A response parser for `cache` just ran: the engine filled the entries and
// ran their callbacks, Escalated included.
void OnResponse(void *cache) {
    // An answer in the world proves the server is answering, so queries may
    // go out now. One behind a loading screen was answered before the
    // transfer, which proves nothing about now.
    if (InWorld())
        g_settled = true;
    for (Slot &s : g_slots) {
        if (s.cache != reinterpret_cast<uintptr_t>(cache))
            continue;
        for (int i = 0; i < Count(s);)
            if (!TryComplete(s, i))
                ++i;
    }
}

// One hook per cache class's response parser: the original fills the entry
// and walks the engine's callback list, then the queue sweeps. `__thiscall`
// mimicked as `__fastcall(ecx, edx_unused, ...)`.
using Parser_t = void(__fastcall *)(void *cache, void *edx, void *packetReader, int flag);

Parser_t g_origItemParser = nullptr;
void __fastcall ItemParser_h(void *cache, void *, void *packetReader, int flag) {
    g_origItemParser(cache, nullptr, packetReader, flag);
    OnResponse(cache);
}

Parser_t g_origCreatureParser = nullptr;
void __fastcall CreatureParser_h(void *cache, void *, void *packetReader, int flag) {
    g_origCreatureParser(cache, nullptr, packetReader, flag);
    OnResponse(cache);
}

Parser_t g_origGameObjectParser = nullptr;
void __fastcall GameObjectParser_h(void *cache, void *, void *packetReader, int flag) {
    g_origGameObjectParser(cache, nullptr, packetReader, flag);
    OnResponse(cache);
}

const Game::HookAutoRegister _hookItem{Offsets::FUN_ITEMSTATS_CACHE_RESPONSE,
                                       reinterpret_cast<void *>(&ItemParser_h),
                                       reinterpret_cast<void **>(&g_origItemParser)};
const Game::HookAutoRegister _hookCreature{Offsets::FUN_CREATURE_QUERY_RESPONSE,
                                           reinterpret_cast<void *>(&CreatureParser_h),
                                           reinterpret_cast<void **>(&g_origCreatureParser)};
const Game::HookAutoRegister _hookGameObject{Offsets::FUN_GAMEOBJECT_QUERY_RESPONSE,
                                             reinterpret_cast<void *>(&GameObjectParser_h),
                                             reinterpret_cast<void **>(&g_origGameObjectParser)};

// The one place any query goes out, so the rate floor governs every path.
//   1. Out of the world nothing is sent, and every query in flight is queued
//      to go again, since the server may have dropped it. Back in the world,
//      queries wait kSettleMs (or the first answer).
//   2. Priority ids go out first, kPriorityPerTick a tick; everything else is
//      background, one per kMinRequestSpacingMs across all caches.
//   3. An id the server refused fails now; a query unanswered for kResendMs
//      goes again, and the id fails after kMaxTimeouts.
void OnWorldTick() {
    const uint32_t now = Time::Clock::NowMs();
    if (!InWorld()) {
        g_settled = false;
        g_inWorldSinceMs = 0;
        for (Slot &s : g_slots)
            for (int i = 0; i < Count(s); ++i)
                s.pending[i].issued = false;
        return;
    }
    if (!g_settled) {
        if (g_inWorldSinceMs == 0)
            g_inWorldSinceMs = now;
        if (Time::Clock::Elapsed(g_inWorldSinceMs, now) >= kSettleMs)
            g_settled = true;
    }

    int priorityBudget = g_settled ? kPriorityPerTick : 0;
    bool backgroundFree =
        g_settled && Time::Clock::Elapsed(g_lastRequestMs, now) >= kMinRequestSpacingMs;
    for (int kind = 0; kind < KIND_COUNT; ++kind) {
        Slot &s = g_slots[kind];
        for (int i = 0; i < Count(s);) {
            Pending &p = s.pending[i];
            if (g_settled && !p.issued) {
                if (s.priority != nullptr && s.priority(p.id)) {
                    if (priorityBudget > 0) {
                        --priorityBudget;
                        Send(kind, p, now);
                    }
                } else if (backgroundFree) {
                    backgroundFree = false;
                    g_lastRequestMs = now;
                    Send(kind, p, now);
                }
            }
            if (TryComplete(s, i))
                continue;
            // Only an id whose query is in flight times out; one still
            // waiting its turn behind the rate floor does not.
            if (p.issued && Time::Clock::Elapsed(p.sentMs, now) >= kResendMs) {
                if (++p.timeouts >= kMaxTimeouts) {
                    const Pending gone = p;
                    RemoveAt(s, i);
                    Report(s, gone, false);
                    continue;
                }
                p.issued = false; // send again
            }
            ++i;
        }
    }
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

} // namespace

Client::Client(Kind kind, DoneFn done, PriorityFn priority) {
    g_slots[kind].done = done;
    g_slots[kind].priority = priority;
}

bool Track(Kind kind, uint32_t id, bool requested) {
    Slot &s = g_slots[kind];
    if (id == 0)
        return false;
    if (Loaded(s, id)) {
        if (requested && s.done != nullptr)
            s.done(id, true, 1, false);
        return true;
    }
    const int i = Find(s, id);
    if (i >= 0) {
        Pending &p = s.pending[i];
        if (requested)
            ++p.requests;
        else
            p.implicit = true;
        return true;
    }
    if (s.count >= kMaxPending)
        return false;
    Pending &p = s.pending[s.count++];
    p = Pending{};
    p.id = id;
    p.requests = requested ? 1 : 0;
    p.implicit = !requested;
    return true;
}

bool IsCached(Kind kind, uint32_t id) {
    return id != 0 && Loaded(g_slots[kind], id);
}

bool Settled() {
    return g_settled;
}

int PendingCount(Kind kind) {
    return Count(g_slots[kind]);
}

} // namespace Cache::QueryLoad
