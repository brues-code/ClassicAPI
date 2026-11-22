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

#include "Game.h"
#include "Offsets.h"
#include "cache/QueryLoad.h"
#include "event/Custom.h"
#include "item/Arg.h"
#include "item/CGItem.h"
#include "item/Data.h"
#include "item/Record.h"
#include "item/ID.h"
#include "item/Location.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <cstdint>
#include <unordered_set>

namespace Item::Data {


// Two events fire on item-data arrival, one per kind of waiter, as the
// 3.80.2 client does it (both kinds hang callbacks on the same id and the
// answer runs them all, FUN_14045BE40):
//
//   `GET_ITEM_INFO_RECEIVED(itemID, success)` — for **implicit** loads:
//       the `GetItemInfo` warmup, our `GetItem*` and `SetItemByID` warmups.
//       One per load however many readers missed (tag dedup 0x14045BDA0),
//       and only for a load that went to the server, never a cached hit.
//
//   `ITEM_DATA_LOAD_RESULT(itemID, success)` — for each
//       `C_Item.RequestLoadItemData(ByID)` call (0x141BA17E0): at once
//       inside the call when the item is cached or the ID is 0 or less,
//       else when the answer arrives, once per call made while it waited.
//
// When a request and an implicit reader both wait on an item, both fire.
//
// Names are reserved at static-init time via `Event::Custom::AutoReserve`;
// the `RebuildEventTable` hook appends them to the engine's input array
// at table-population time, so the engine itself allocates and tracks
// our entries identically to its own.
static constexpr const char *kItemDataLoadResult = "ITEM_DATA_LOAD_RESULT";
static constexpr const char *kGetItemInfoReceived = "GET_ITEM_INFO_RECEIVED";
static const Game::Doc::Field kLoadResultPayload[] = {
    Game::Doc::Req("itemID", "number"),
    Game::Doc::Req("success", "bool", "False when the server did not send the data."),
};
static const Game::Doc::Event kItemDataLoadResultDoc{
    "Item",
    "Fires when item data asked for with C_Item.RequestLoadItemData or "
    "C_Item.RequestLoadItemDataByID loads or fails to load.",
    kLoadResultPayload};
static const Game::Doc::Event kGetItemInfoReceivedDoc{
    "Item",
    "Fires when item data that GetItemInfo or another item reader asked for loads or fails "
    "to load.",
    kLoadResultPayload};
static const Event::Custom::AutoReserve _reserveItemDataLoadResult{kItemDataLoadResult,
                                                                   &kItemDataLoadResultDoc};
static const Event::Custom::AutoReserve _reserveGetItemInfoReceived{kGetItemInfoReceived,
                                                                    &kGetItemInfoReceivedDoc};

static void FireGetItemInfoReceived(int itemID, bool success) {
    Event::Custom::FireIdSuccess(_reserveGetItemInfoReceived.Slot(), itemID,
                                 success);
}

static void FireItemDataLoadResult(int itemID, bool success) {
    Event::Custom::FireIdSuccess(_reserveItemDataLoadResult.Slot(), itemID,
                                 success);
}

// Resolves an item-location table at stack idx to the itemID by walking
// `Location::Resolve` -> CGItem -> instance block (+0x08) -> itemID (+0x0C).
// Returns 0 if the slot is empty or the table is malformed.
static int ResolveLocationToItemID(void *L, int idx) {
    return Item::ID::FromCGItem(Item::Location::Resolve(L, idx));
}

static bool IsCached(int itemID) {
    return itemID > 0 &&
           Cache::QueryLoad::IsCached(Cache::QueryLoad::KIND_ITEM, static_cast<uint32_t>(itemID));
}

// The 3.80.2 itemInfo reader (FUN_140B0BDE0) takes a number or a string and
// raises the usage error for anything else, a missing argument included.
static bool IsItemInfoArg(void *L, int idx) {
    const int type = Game::Lua::Type(L, idx);
    return type == Game::Lua::TYPE_NUMBER || type == Game::Lua::TYPE_STRING;
}

// The 3.80.2 itemLocation reader (FUN_140B3CEA0) raises the usage error for
// a malformed location and for one that holds no item. 0 after the error.
static int LocationItemIDOrError(void *L, const char *usage) {
    const int itemID =
        Item::Location::IsLocationArg(L, 1) ? ResolveLocationToItemID(L, 1) : 0;
    if (itemID <= 0)
        Game::Lua::Error(L, usage);
    return itemID;
}

static int __fastcall Script_IsItemDataCachedByID(void *L) {
    if (!IsItemInfoArg(L, 1)) {
        Game::Lua::Error(L, "Usage: local isCached = C_Item.IsItemDataCachedByID(itemInfo)");
        return 0;
    }
    Game::Lua::PushBool(L, IsCached(Item::Arg::ResolveItemID(L, 1)));
    return 1;
}

static int __fastcall Script_IsItemDataCached(void *L) {
    const int itemID =
        LocationItemIDOrError(L, "Usage: local isCached = C_Item.IsItemDataCached(itemLocation)");
    if (itemID <= 0)
        return 0;
    Game::Lua::PushBool(L, IsCached(itemID));
    return 1;
}

// Loads go through Cache::QueryLoad, which owns the world gate, the resend,
// the issue #25 rate floor and the response hook for every query cache. This
// module adds the item side: the two events, and the player's carried items
// as the priority set.
//
// Both paths into the queue route through `Cache::QueryLoad::Track`:
//   - explicit `C_Item.RequestLoadItemData(ByID)`: requested, fires
//     ITEM_DATA_LOAD_RESULT;
//   - implicit `WarmCache` (the `GetItemInfo` hook + every `GetItem*`
//     getter's cache-miss warmup): fires GET_ITEM_INFO_RECEIVED.

constexpr int kMaxOwned = 256;
constexpr uint32_t kOwnedRefreshMs = 1000; // carried set changes slowly

// Snapshot of the itemIDs the player currently carries. Rebuilt by a
// pure-C inventory walk (no Lua stack — safe at world-tick time) at most
// once per kOwnedRefreshMs, and only while there is queued work.
static uint32_t g_owned[kMaxOwned];
static int g_ownedCount = 0;
static uint32_t g_ownedBuiltMs = 0;
static bool g_ownedEverBuilt = false;
// One-shot latch: have we proactively seeded the queue with the player's
// carried items this enter-world session? Re-armed per in-game init (login
// or /reload) in RegisterLuaFunctions.
static bool g_ownedPrefetched = false;

// The items a model on screen waits to put on (WarmCacheShown), until each
// resolves.
static std::unordered_set<uint32_t> g_shown;

static void OnLoadDone(uint32_t itemID, bool success, int requests, bool implicit) {
    g_shown.erase(itemID);
    for (int i = 0; i < requests; ++i)
        FireItemDataLoadResult(static_cast<int>(itemID), success);
    if (implicit)
        FireGetItemInfoReceived(static_cast<int>(itemID), success);
    for (auto *sub = LoadSubscriber::head; sub != nullptr; sub = sub->next)
        sub->fn(itemID, success);
}

// --- Owned-item priority set --------------------------------------------
//
// Determines which pending items are the player's carried inventory so the
// queue issues them ahead of background bulk lookups: on login the bag and
// paperdoll UI show them at once, and behind the background floor they
// showed as "unknown" for a second or two while an addon scan drained ahead
// of them. The walk reads the engine's GUID arrays directly (no Lua stack,
// no GetItemBySlot bank gate) — the same pure-C pattern `item/Count` uses —
// so it is safe to run at world-tick time.

static void AddOwned(uint32_t itemID) {
    if (itemID == 0)
        return;
    for (int i = 0; i < g_ownedCount; ++i)
        if (g_owned[i] == itemID)
            return; // dedup — stacks / duplicates collapse to one entry
    if (g_ownedCount < kMaxOwned)
        g_owned[g_ownedCount++] = itemID;
}

// Collects itemIDs from linear slots [first, last] of an inventory
// manager's GUID array (invMgr+OFF_INVMGR_GUID_ARRAY). Empty slots hold a
// zero GUID and are skipped.
static void CollectOwnedFrom(const uint8_t *invMgr, int first, int last) {
    if (invMgr == nullptr)
        return;
    auto *guids = Game::Read<const uint64_t *>(
        invMgr, Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr)
        return;
    for (int slot = first; slot <= last; ++slot) {
        const uint8_t *item = Item::Location::ResolveByGUID(guids[slot]);
        if (item != nullptr)
            AddOwned(static_cast<uint32_t>(Item::ID::FromCGItem(item)));
    }
}

static void RebuildOwned() {
    g_ownedCount = 0;
    auto *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr != nullptr) {
        // Player invMgr linear slots 0..38: equipment + bag-slot items +
        // backpack (bank starts at INVMGR_BANK_MAIN_FIRST_SLOT = 39).
        CollectOwnedFrom(invMgr, 0,
                         Offsets::INVMGR_BACKPACK_FIRST_SLOT + Offsets::BACKPACK_NUM_SLOTS - 1);
        // Contents of each equipped bag (its own invMgr, slot count at +0).
        for (int bagID = 1; bagID <= 4; ++bagID) {
            auto *bagInv =
                static_cast<const uint8_t *>(Item::Location::BagInventory(bagID));
            if (bagInv == nullptr)
                continue;
            const int count = static_cast<int>(
                Game::Read<uint32_t>(bagInv, Offsets::OFF_INVMGR_SLOT_COUNT));
            if (count > 0)
                CollectOwnedFrom(bagInv, 0, count - 1);
        }
    }
    g_ownedEverBuilt = true;
}

static bool IsOwned(uint32_t itemID) {
    for (int i = 0; i < g_ownedCount; ++i)
        if (g_owned[i] == itemID)
            return true;
    return false;
}

// The priority lane: the player's carried items, and the items a model on
// screen waits for.
static bool IsPriority(uint32_t itemID) { return IsOwned(itemID) || g_shown.count(itemID) != 0; }

static const Cache::QueryLoad::Client _client{Cache::QueryLoad::KIND_ITEM, &OnLoadDone,
                                              &IsPriority};

static bool Track(uint32_t itemID, bool requested) {
    return Cache::QueryLoad::Track(Cache::QueryLoad::KIND_ITEM, itemID, requested);
}

static void OnWorldTick() {
    if (!Cache::QueryLoad::Settled())
        return;
    const uint32_t now = Time::Clock::NowMs();

    // Proactive local-inventory prefetch (once per enter-world session).
    // The engine does NOT query bag item stats on enter-world or bag draw:
    // GetContainerItemInfo and the icon getter both do a null-callback
    // (pure lookup) _GetRecord, and an uncached bag item resolves
    // displayInfoID = 0, which the icon path draws as INV_Misc_QuestionMark
    // ("?"). (Equipped items are exempt — their icon comes from the
    // server-pushed visible-items table.) So on a cold WDB the player's
    // carried items stay "?" until something else queries them — a hover, a
    // loot, or an addon's GetItemInfo. Seed the queue with every carried
    // item so they issue on the priority lane ahead of any addon's
    // background flood. Bounded (<= kMaxOwned) and issued under the
    // priority budget, so it adds no burst (issue #25). Waits for the queue
    // to settle, so the inventory is in place; gate on a live inventory
    // manager so a too-early settle retries next tick instead of latching on
    // an empty walk.
    if (!g_ownedPrefetched && Unit::Identity::PlayerInventoryManager() != nullptr) {
        RebuildOwned();
        g_ownedBuiltMs = now;
        for (int i = 0; i < g_ownedCount; ++i)
            Track(g_owned[i], /*requested=*/false);
        g_ownedPrefetched = true;
    }

    // Refresh the carried-item set while there is queued work, at most
    // once per kOwnedRefreshMs — the set changes slowly and the walk is
    // pointer-chasing, not free.
    if (Cache::QueryLoad::PendingCount(Cache::QueryLoad::KIND_ITEM) > 0 &&
        (!g_ownedEverBuilt || Time::Clock::Elapsed(g_ownedBuiltMs, now) >= kOwnedRefreshMs)) {
        RebuildOwned();
        g_ownedBuiltMs = now;
    }
}

static const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// Public C++ API — see Item/Data.h. Implicit (transparent) warmup path
// used by the `Script_GetItemInfo` hook and `SetItemByID`. Routes every
// call through Cache::QueryLoad, whose single rate-limited issue point
// governs all outgoing queries (issue #25). An already-cached item resolves
// for free (a pure lookup, no SMSG); only a genuine miss is queued, and its
// answer fires `GET_ITEM_INFO_RECEIVED` (and `ITEM_DATA_LOAD_RESULT` too for
// each request that waited on it).
bool WarmCacheShown(uint32_t itemID) {
    if (itemID == 0 || IsCached(itemID))
        return itemID != 0;
    g_shown.insert(itemID);
    return Track(itemID, /*requested=*/false);
}

bool WarmCache(uint32_t itemID) {
    // Do NOT issue the query here. Two reasons converge on the queue:
    //   - Network: a post-settle immediate CacheFetch let a reload-time
    //     lookup burst fire one SMSG per item in a single frame and flood
    //     the network (issue #25). The queue's rate limiter prevents it.
    //   - World gate: a query sent before the server has the player in the
    //     world is dropped, and the node it created stays pending, leaving
    //     the item permanently nil. This broke pfQuest/Questie scanning at
    //     PLAYER_ENTERING_WORLD. The queue issues only once settled, and
    //     resends what was lost, so deferring here fixes that too.
    return Track(itemID, /*requested=*/false);
}

const uint8_t *RecordOrRequest(uint32_t itemID) {
    if (const uint8_t *record = Item::PeekRecord(itemID))
        return record;
    WarmCache(itemID);
    return nullptr;
}

// Hook for `Script_GetItemInfo`. Pre-warms the cache from arg 1, then
// dispatches to the original. Effect: the original still returns nil
// for cache misses (vanilla behavior), but a query is now in flight,
// so subsequent calls will return valid data and
// GET_ITEM_INFO_RECEIVED will fire.
Script_GetItemInfo_t Script_GetItemInfo_o = nullptr;

int __fastcall Script_GetItemInfo_h(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID > 0)
        WarmCache(static_cast<uint32_t>(itemID));
    return Script_GetItemInfo_o(L);
}

// Both return nothing (FUN_140CEA0A0 / FUN_140CEA280). An ID of 0 or less is
// known missing: ITEM_DATA_LOAD_RESULT(id, false) at once (FUN_141382130's
// synchronous event 1).
static void RequestLoad(int itemID) {
    // A full queue fails the request now, so every call still fires its one
    // result.
    if (itemID <= 0 || !Track(static_cast<uint32_t>(itemID), /*requested=*/true))
        FireItemDataLoadResult(itemID, false);
}

static int __fastcall Script_RequestLoadItemDataByID(void *L) {
    if (!IsItemInfoArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.RequestLoadItemDataByID(itemInfo)");
        return 0;
    }
    RequestLoad(Item::Arg::ResolveItemID(L, 1));
    return 0;
}

static int __fastcall Script_RequestLoadItemData(void *L) {
    const int itemID =
        LocationItemIDOrError(L, "Usage: C_Item.RequestLoadItemData(itemLocation)");
    if (itemID > 0)
        RequestLoad(itemID);
    return 0;
}

static const Game::Doc::Field kByIDArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kLocationArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kIsCachedRets[] = {
    Game::Doc::Req("isCached", "bool"),
};
static const Game::Doc::Function kIsItemDataCachedByID{
    "Returns whether the item's data is loaded, without asking the server for it.", kByIDArgs,
    kIsCachedRets};
static const Game::Doc::Function kIsItemDataCached{
    "Returns whether the data of the item at a location is loaded, without asking the server "
    "for it.",
    kLocationArgs, kIsCachedRets};
static const Game::Doc::Function kRequestLoadItemDataByID{
    "Asks the server for the item's data, then fires ITEM_DATA_LOAD_RESULT.", kByIDArgs, {}};
static const Game::Doc::Function kRequestLoadItemData{
    "Asks the server for the data of the item at a location, then fires ITEM_DATA_LOAD_RESULT.",
    kLocationArgs, {}};

static void RegisterLuaFunctions() {
    // Re-arm the proactive carried-item prefetch for this enter-world
    // session. Fires on login and /reload (this callback runs per in-game
    // Lua init). On a /reload the cache is warm, so the re-run resolves as
    // cache hits and does nothing.
    g_ownedPrefetched = false;
    Game::Lua::RegisterTableFunction("C_Item", "IsItemDataCachedByID",
                                     &Script_IsItemDataCachedByID, &kIsItemDataCachedByID);
    Game::Lua::RegisterTableFunction("C_Item", "IsItemDataCached", &Script_IsItemDataCached,
                                     &kIsItemDataCached);
    Game::Lua::RegisterTableFunction("C_Item", "RequestLoadItemDataByID",
                                     &Script_RequestLoadItemDataByID, &kRequestLoadItemDataByID);
    Game::Lua::RegisterTableFunction("C_Item", "RequestLoadItemData", &Script_RequestLoadItemData,
                                     &kRequestLoadItemData);
    // Event names are reserved at file scope via `AutoReserve`; nothing
    // to do here for events.
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Auto-warm the item cache on `GetItemInfo(uncached_id)` calls, so
// subsequent calls return valid data and GET_ITEM_INFO_RECEIVED
// fires when the response arrives — matches modern WoW (5.x+)
// behavior. Without this, vanilla 1.12's `GetItemInfo` returns nil
// for misses and never fires a query, forcing addons to roll their
// own warmup hacks.
static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_SCRIPT_GET_ITEM_INFO,
    reinterpret_cast<void *>(&Script_GetItemInfo_h),
    reinterpret_cast<void **>(&Script_GetItemInfo_o)};

} // namespace Item::Data
