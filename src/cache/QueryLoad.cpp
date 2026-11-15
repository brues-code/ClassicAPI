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
#include "cache/Record.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"

#include <cstdint>

namespace Cache::QueryLoad {

namespace {

// A cache class's response parser, which we hook (e.g.
// FUN_CREATURE_QUERY_RESPONSE / FUN_GAMEOBJECT_QUERY_RESPONSE).
using CacheResponse_t = void(__thiscall *)(void *cache, void *packetReader, int flag);

// Engine callback shape (__stdcall, ret 8) — see Item::Data. We only pass
// it to make `_GetRecord` send the query (the trigger is callback != 0);
// completion is observed via the response hook + tick sweep.
void __stdcall NoOpCallback(void * /*userData*/, int /*success*/) {}

// A class's `_GetRecord` (Cache::Record). With dedup, repeat fetches for the
// same id before the response lands don't queue the callback twice.
const uint8_t *CacheFetch(uintptr_t getRecord, void *cache, uint32_t id, void *callback) {
    const uint64_t zeroGuid = 0;
    void *userData =
        callback ? reinterpret_cast<void *>(static_cast<uintptr_t>(id)) : nullptr;
    return Cache::Record::Get(getRecord, reinterpret_cast<uintptr_t>(cache), id, &zeroGuid,
                              callback, userData, true);
}

struct Pending {
    uint32_t id;
    int ticksWaiting;
};

constexpr int kMaxCaches = 8;
constexpr int kMaxPending = 256;
constexpr int kMaxWaitTicks = 1200; // give up and fire failure after this many WorldTicks

struct Registered {
    void *cache;
    const Event::Custom::AutoReserve *reserve; // completion event's reservation
    uintptr_t getRecord;                       // this cache class's _GetRecord
    Pending pending[kMaxPending];
    int pendingCount;
};

Registered g_caches[kMaxCaches];
int g_cacheCount = 0;

Registered *Find(void *cache) {
    for (int i = 0; i < g_cacheCount; ++i) {
        if (g_caches[i].cache == cache)
            return &g_caches[i];
    }
    return nullptr;
}

void Fire(const Registered *r, uint32_t id, bool success) {
    Event::Custom::FireIdSuccess(r->reserve->Slot(), static_cast<int>(id), success);
}

int FindPending(const Registered *r, uint32_t id) {
    for (int i = 0; i < r->pendingCount; ++i) {
        if (r->pending[i].id == id)
            return i;
    }
    return -1;
}

void RemovePendingAt(Registered *r, int idx) {
    --r->pendingCount;
    if (idx != r->pendingCount)
        r->pending[idx] = r->pending[r->pendingCount];
}

bool IsLoaded(const Registered *r, uint32_t id) {
    return CacheFetch(r->getRecord, r->cache, id, nullptr) != nullptr;
}

// Fire + remove any pending id whose entry is now loaded.
void Sweep(Registered *r) {
    for (int i = 0; i < r->pendingCount;) {
        if (IsLoaded(r, r->pending[i].id)) {
            const uint32_t id = r->pending[i].id;
            RemovePendingAt(r, i);
            Fire(r, id, true);
            continue;
        }
        ++i;
    }
}

// Shared post-response dispatch: a parser for `cache` just ran, so sweep
// that cache's pending set (no-op for caches we don't track).
void DispatchSweep(void *cache) {
    Registered *r = Find(cache);
    if (r != nullptr)
        Sweep(r);
}

// One response-handler hook per cache class. Each calls its own original
// trampoline (the parser fills the entry + walks the engine's own
// callback list), then sweeps via the shared dispatch. `__thiscall`
// mimicked as `__fastcall(ecx, edx_unused, …)`.
using Trampoline_t = void(__fastcall *)(void *cache, void *edx, void *packetReader, int flag);

CacheResponse_t g_origCreatureParser = nullptr;
void __fastcall CreatureParser_h(void *cache, void * /*edx*/, void *packetReader, int flag) {
    reinterpret_cast<Trampoline_t>(g_origCreatureParser)(cache, nullptr, packetReader, flag);
    DispatchSweep(cache);
}

CacheResponse_t g_origGameObjectParser = nullptr;
void __fastcall GameObjectParser_h(void *cache, void * /*edx*/, void *packetReader, int flag) {
    reinterpret_cast<Trampoline_t>(g_origGameObjectParser)(cache, nullptr, packetReader, flag);
    DispatchSweep(cache);
}

const Game::HookAutoRegister _hookCreature{
    Offsets::FUN_CREATURE_QUERY_RESPONSE, reinterpret_cast<void *>(&CreatureParser_h),
    reinterpret_cast<void **>(&g_origCreatureParser)};
const Game::HookAutoRegister _hookGameObject{
    Offsets::FUN_GAMEOBJECT_QUERY_RESPONSE, reinterpret_cast<void *>(&GameObjectParser_h),
    reinterpret_cast<void **>(&g_origGameObjectParser)};

// Timeout + safety net. The hooks cover the happy path; this fires
// failure for ids that never resolve after `kMaxWaitTicks`, and
// re-checks loaded as a backstop for fills observed off the hook path.
void OnWorldTick() {
    for (int c = 0; c < g_cacheCount; ++c) {
        Registered *r = &g_caches[c];
        for (int i = 0; i < r->pendingCount;) {
            if (IsLoaded(r, r->pending[i].id)) {
                const uint32_t id = r->pending[i].id;
                RemovePendingAt(r, i);
                Fire(r, id, true);
                continue;
            }
            if (r->pending[i].ticksWaiting >= kMaxWaitTicks) {
                const uint32_t id = r->pending[i].id;
                RemovePendingAt(r, i);
                Fire(r, id, false);
                continue;
            }
            ++r->pending[i].ticksWaiting;
            ++i;
        }
    }
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

} // namespace

void Register(void *cacheInstance, const Event::Custom::AutoReserve *reserve,
              uintptr_t getRecordAddr) {
    if (cacheInstance == nullptr || reserve == nullptr || getRecordAddr == 0)
        return;
    if (Find(cacheInstance) != nullptr) // idempotent (safe across /reload)
        return;
    if (g_cacheCount >= kMaxCaches)
        return;
    g_caches[g_cacheCount].cache = cacheInstance;
    g_caches[g_cacheCount].reserve = reserve;
    g_caches[g_cacheCount].getRecord = getRecordAddr;
    g_caches[g_cacheCount].pendingCount = 0;
    ++g_cacheCount;
}

bool IsCached(void *cacheInstance, uint32_t id) {
    const Registered *r = Find(cacheInstance);
    return r != nullptr && IsLoaded(r, id);
}

bool RequestLoad(void *cacheInstance, uint32_t id) {
    Registered *r = Find(cacheInstance);
    if (r == nullptr || id == 0)
        return false;

    // Already cached — fire success synchronously (modern RequestLoad
    // contract: the completion event fires even on a cache hit).
    if (IsLoaded(r, id)) {
        Fire(r, id, true);
        return true;
    }
    // Already in flight — one completion event per id.
    if (FindPending(r, id) >= 0)
        return true;
    if (r->pendingCount >= kMaxPending)
        return false;

    // Send the query (non-null callback is the trigger), then track until
    // the response hook / tick fires the event.
    CacheFetch(r->getRecord, cacheInstance, id, reinterpret_cast<void *>(&NoOpCallback));
    r->pending[r->pendingCount].id = id;
    r->pending[r->pendingCount].ticksWaiting = 0;
    ++r->pendingCount;
    return true;
}

} // namespace Cache::QueryLoad
