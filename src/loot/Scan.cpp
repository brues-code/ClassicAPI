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
// Hook-driven loot corpse-walker. Walks the nearby-lootable-units set,
// opens each corpse's loot session via `FUN_CMSG_LOOT_UNIT`, then
// *intercepts the engine's loot-window response* before any visual updates
// happen. The intercept (`FUN_LOOT_CONTROLLER`) lets the original function
// run so `VAR_LOOT_SLOTS` populates and the link builder works — but a
// paired hook on `FUN_FIRE_EVENT_NO_ARGS` swallows the `LOOT_OPENED` /
// `LOOT_CLOSED` event fires while the walk is in progress. Net result: the
// engine's state machine cycles silently for each corpse and `LootFrame`
// never reacts. Other Lua addons listening to those events also see nothing
// during the walk; that's deliberate.
//
// Two Lua entry points share this one walker (and, importantly, this one
// `FUN_LOOT_CONTROLLER` hook — MinHook allows a single hook per target, so
// they can't be separate modules):
//   - `C_Loot.ScanNearbyLoot()` — scrape-only: read each corpse's contents
//     into `GetLastScanResults()` and release without looting.
//   - `C_Loot.LootAllCorpses([max])` — the same walk but actually LOOTS
//     each window (coin via `CMSG_LOOT_MONEY`, every item slot via
//     `CMSG_LOOT_ITEM`) before releasing. Silent (no LootFrame flicker);
//     the usual item-received / money chat + inventory events still fire,
//     since only the LootFrame open/close events are swallowed.
//
// Compared to the older polling-driven version this also collapses the
// state machine — we no longer need `WaitingOpen` / `WaitingClose` /
// per-tick `VAR_LOOT_GUID` reads, because the controller hook *is* the
// state transition.

#include "../Game.h"
#include "../Offsets.h"
#include "../event/Custom.h"
#include "../event/SignalHook.h"
#include "../guid/Guid.h"
#include "../object/Resolve.h"
#include "../tick/WorldTick.h"
#include "Request.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Loot::Scan {

namespace {

constexpr const char *kEventCompleted = "LOOT_SCAN_COMPLETED";
const Event::Custom::AutoReserve _reserve{kEventCompleted};

// Per-step timeout for the controller hook to fire on a queued
// CMSG_LOOT. WorldTick fires at the client frame rate (~30-60 Hz),
// so 180 ticks ≈ 3-6s wall clock. Long enough for high-latency
// servers, short enough that a single dropped response doesn't hang
// the whole scan.
constexpr int kStepTimeoutTicks = 180;

// Event IDs the engine fires through `FUN_FIRE_EVENT_NO_ARGS` for
// loot-window transitions. The hook swallows these only when the
// scanner has the suppression flag raised — normal play sees them
// fire unmodified.
constexpr int EVENT_LOOT_OPENED = 0x10B;
constexpr int EVENT_LOOT_CLOSED = 0x10D;

enum class State {
    Idle,
    WaitingForResponse, // CMSG_LOOT sent; awaiting our controller hook
};

struct SlotItem {
    uint32_t itemID;
    uint32_t count;
    std::string link; // empty if the cache record wasn't loaded
};

struct LootEntry {
    uint64_t guid;
    uint32_t coin; // copper amount; 0 if no coin
    std::vector<SlotItem> items;
};

struct ScanCtx {
    State state = State::Idle;
    void *player = nullptr;
    uint64_t currentGuid = 0;
    int ticksSinceTransition = 0;
    bool suppressEvents = false; // read by the FireEvent hook
    bool lootMode = false;       // LootAllCorpses (true) vs ScanNearbyLoot (false)
    std::vector<uint64_t> queue;
    std::vector<LootEntry> results;
};

ScanCtx s_scan;

// === Typedefs for engine helpers ===

using ClntObjMgrEnumVisibleObjectsCallback_t = int(__fastcall *)(void *ctx,
                                                                  void *unusedEdx,
                                                                  uint64_t guid);
using ClntObjMgrEnumVisibleObjects_t =
    int(__fastcall *)(ClntObjMgrEnumVisibleObjectsCallback_t cb, void *ctx);
using CloseLootInner_t = void(__fastcall *)(int sendRelease,
                                             char earlyReturnOnGameObject,
                                             char showError);
using LootSlotLinkBuilder_t = const char *(__fastcall *)(uint32_t userFacingSlot,
                                                          char *outBuf,
                                                          int bufSize);
// `FUN_CMSG_LOOT_ITEM` — `__stdcall(uint8_t wireSlot)`, sends CMSG_LOOT_ITEM
// for one slot of the open window (see the offset comment for the
// __stdcall-vs-__fastcall stack-corruption hazard).
using SendLootItem_t = void(__stdcall *)(uint8_t wireSlot);
// `FUN_CMSG_LOOT_MONEY` — `__fastcall(player)`, sends the coin request.
using LootMoney_t = void(__fastcall *)(void *player);
// `FUN_LOOT_CONTROLLER` is `__fastcall(int, undefined4, int)` — three
// args: lootStructPtr in ECX, coin in EDX, an extra unk on stack[0].
// Mis-declaring as 4-arg __fastcall would push one phantom stack slot
// the engine doesn't pop, drifting ESP by 4 bytes per call until a
// later return reads `0x00000001` as a corrupt frame pointer and the
// process crashes. We do see / forward the `coin` value because the
// engine reads it as `DAT_00b71ba0` later.
using LootController_t = void(__fastcall *)(int lootStructPtr,
                                             int coin,
                                             int unk);
struct C3Vector {
    float x, y, z;
};
using GetPosition_t = const C3Vector *(__thiscall *)(const void *self,
                                                      C3Vector *outBuf);
constexpr int VTBL_GET_POSITION_OFFSET = 0x14;

constexpr float INTERACT_REACH = 1.3333333f;
constexpr float MIN_INTERACT_RANGE = 5.0f;

// === Range / lootability helpers (identical to Loot::Nearby) ===

bool IsLootableUnit(const void *unit) {
    auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    const uint32_t flags =
        Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_DYNAMIC_FLAGS);
    return (flags & Offsets::UNIT_DYNFLAG_LOOTABLE) != 0;
}

float BoundingRadius(const void *unit) {
    auto *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    return Game::Read<float>(fields, Offsets::OFF_UNIT_FIELD_BOUNDING_RADIUS);
}

const C3Vector *GetPosition(const void *unit, C3Vector *outBuf) {
    auto vtable = *reinterpret_cast<void *const *const *>(unit);
    auto fn = reinterpret_cast<GetPosition_t>(
        *reinterpret_cast<const void *const *>(
            reinterpret_cast<const uint8_t *>(vtable) +
            VTBL_GET_POSITION_OFFSET));
    return fn(unit, outBuf);
}

bool InInteractRange(const void *player, const void *target) {
    C3Vector pBuf{};
    C3Vector tBuf{};
    const C3Vector *pPos = GetPosition(player, &pBuf);
    const C3Vector *tPos = GetPosition(target, &tBuf);
    if (pPos == nullptr || tPos == nullptr)
        return false;
    const float dx = pPos->x - tPos->x;
    const float dy = pPos->y - tPos->y;
    const float dz = pPos->z - tPos->z;
    const float distSq = dx * dx + dy * dy + dz * dz;
    float range = BoundingRadius(player) + BoundingRadius(target) + INTERACT_REACH;
    if (range < MIN_INTERACT_RANGE)
        range = MIN_INTERACT_RANGE;
    return distSq <= range * range;
}

// === Scrape ===

// Reads the post-population `VAR_LOOT_SLOTS` table into a LootEntry.
// The controller hook calls this *after* letting the original
// `FUN_LOOT_CONTROLLER` run, so the slot table is fully filled in
// and the link builder works against it.
void ScrapeCurrentLoot(LootEntry *out) {
    const uint32_t coinRaw = Game::Read<uint32_t>(Offsets::VAR_LOOT_LOOTABLE);
    out->coin = (coinRaw == 0xFFFFFFFFu) ? 0u : coinRaw;

    const bool hasCoin = coinRaw != 0;
    auto LinkBuilder = reinterpret_cast<LootSlotLinkBuilder_t>(
        Offsets::FUN_LOOT_SLOT_LINK_BUILDER);

    for (int slot = 0; slot < Offsets::LOOT_MAX_SLOTS; ++slot) {
        auto *entry = reinterpret_cast<const uint8_t *>(
            Offsets::VAR_LOOT_SLOTS + slot * Offsets::LOOT_SLOT_STRIDE);
        const uint32_t itemID = *reinterpret_cast<const uint32_t *>(entry);
        if (itemID == 0)
            continue;
        const uint32_t count = *reinterpret_cast<const uint32_t *>(entry + 8);

        SlotItem item;
        item.itemID = itemID;
        item.count = count;

        char linkBuf[0x400];
        const uint32_t slotArg = hasCoin ? static_cast<uint32_t>(slot + 1)
                                          : static_cast<uint32_t>(slot);
        const char *link = LinkBuilder(slotArg, linkBuf, sizeof linkBuf);
        if (link != nullptr)
            item.link = link;

        out->items.push_back(std::move(item));
    }
}

// Loots everything in the currently-open window: coin (if any) plus every
// populated item slot. Called from the controller hook (loot mode) after the
// original populated `VAR_LOOT_SLOTS`, so the slot table is fully filled in.
//
// Items go through `FUN_CMSG_LOOT_ITEM` directly — the same silent path
// `C_Loot.LootUnitItem` uses, which bypasses the BoP/unique confirm dialog
// the Lua `LootSlot` wrapper would raise (appropriate for a programmatic
// loot-all; the server still enforces real permissions). Coin goes through
// `FUN_CMSG_LOOT_MONEY` with `s_scan.player` — the SAME pointer we passed to
// `FUN_CMSG_LOOT_UNIT`, whose send wrote this window's target GUID to the
// player's `+0x1D28` guard slot, so the money send's guard passes.
//
// Packet order to the server is items, then coin, then the release that
// follows (`SendCloseLoot`); the server processes loot-item / loot-money
// before the release, so nothing is dropped.
void LootCurrentWindow() {
    // Coin present when `VAR_LOOT_LOOTABLE` holds a real amount (0 = none;
    // 0xFFFFFFFF = already-looted sentinel, never seen on a fresh open).
    const uint32_t coinRaw = Game::Read<uint32_t>(Offsets::VAR_LOOT_LOOTABLE);
    if (coinRaw != 0 && coinRaw != 0xFFFFFFFFu) {
        auto LootMoney = reinterpret_cast<LootMoney_t>(Offsets::FUN_CMSG_LOOT_MONEY);
        LootMoney(s_scan.player);
    }

    auto SendItem = reinterpret_cast<SendLootItem_t>(Offsets::FUN_CMSG_LOOT_ITEM);
    for (int slot = 0; slot < Offsets::LOOT_MAX_SLOTS; ++slot) {
        auto *entry = reinterpret_cast<const uint8_t *>(
            Offsets::VAR_LOOT_SLOTS + slot * Offsets::LOOT_SLOT_STRIDE);
        const uint32_t itemID = *reinterpret_cast<const uint32_t *>(entry);
        if (itemID == 0)
            continue;
        SendItem(*(entry + Offsets::OFF_LOOT_SLOT_WIRE_INDEX));
    }
}

// === State-machine transitions ===

void StartLoot(uint64_t guid);
void TryStartNext();

void SendCloseLoot() {
    auto Close = reinterpret_cast<CloseLootInner_t>(Offsets::FUN_CLOSE_LOOT_INNER);
    Close(1, '\0', '\0');
}

void Complete() {
    s_scan.state = State::Idle;
    s_scan.currentGuid = 0;
    s_scan.suppressEvents = false;
    s_scan.lootMode = false;
    Event::Custom::Fire(_reserve.Slot(), "");
}

void TryStartNext() {
    if (s_scan.queue.empty()) {
        Complete();
        return;
    }
    uint64_t guid = s_scan.queue.back();
    s_scan.queue.pop_back();
    StartLoot(guid);
}

void StartLoot(uint64_t guid) {
    void *target = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    // Not sent (mounted mid-walk, or the engine's pre-checks refused): no
    // response is coming, so skip without waiting out the timeout.
    if (target == nullptr || !Request::Send(s_scan.player, target, guid)) {
        TryStartNext();
        return;
    }
    s_scan.currentGuid = guid;
    s_scan.state = State::WaitingForResponse;
    s_scan.ticksSinceTransition = 0;
}

// === Hooks ===

LootController_t s_lootController_o = nullptr;

// `LOOT_OPENED` / `LOOT_CLOSED` get suppressed only while a scan is
// actively interleaving open/close cycles, signaled by
// `s_scan.suppressEvents`. The flag is raised by the controller hook
// just around the original-loot-controller call and the
// `SendCloseLoot` that follows, then dropped before
// `TryStartNext`-initiated CMSG_LOOTs go out. Every other engine
// event fire — and normal play's loot events — pass through.
//
// Runs as an interceptor on the shared no-arg event hook
// (`Event::SignalHook`) rather than owning the hook directly, so it can
// coexist with other features that intercept the same dispatcher (e.g. the
// PLAYER_ENTERING_WORLD payload). Return true to swallow.
bool SuppressLootEvents(int eventID) {
    return s_scan.suppressEvents &&
           (eventID == EVENT_LOOT_OPENED || eventID == EVENT_LOOT_CLOSED);
}

// Intercepts the engine's loot-window state controller. Two paths:
//
//   1. **Scan-driven open** — the incoming `lootStructPtr` carries a
//      GUID matching our expected one. Let the original run (so the
//      slot table populates), then scrape, send a release (which
//      will recursively re-enter via `FUN_0048F200` → close-path
//      `FUN_LOOT_CONTROLLER(NULL, …)`), and advance the scan. Both
//      the inner open and the recursive close fire their respective
//      events through our `FireEventNoArgs_h`, which swallows them
//      while `suppressEvents` is on.
//
//   2. **Anything else** — pass through unmodified. Normal loot
//      interactions outside a scan are unaffected; the inner-close
//      call generated by our scan's `SendCloseLoot` re-enters here
//      with `arg1 == 0` and is also a passthrough (its
//      suppression-flag check happens at the FireEvent hook layer).
void __fastcall LootController_h(int lootStructPtr, int coin, int unk) {
    const bool scanOpenCall =
        s_scan.state == State::WaitingForResponse &&
        lootStructPtr != 0 &&
        [&] {
            // Read the GUID off the response struct (`+8` deref to a
            // `{uint32 lo; uint32 hi}` block — same shape the original
            // copies into `VAR_LOOT_GUID_LO/HI`).
            auto *guidPtr = *reinterpret_cast<const uint32_t *const *>(
                reinterpret_cast<const uint8_t *>(
                    static_cast<intptr_t>(lootStructPtr)) + 8);
            if (guidPtr == nullptr) return false;
            const uint64_t guid =
                static_cast<uint64_t>(guidPtr[0]) |
                (static_cast<uint64_t>(guidPtr[1]) << 32);
            return guid == s_scan.currentGuid;
        }();

    if (scanOpenCall) {
        s_scan.suppressEvents = true;
        s_lootController_o(lootStructPtr, coin, unk);

        LootEntry entry;
        entry.guid = s_scan.currentGuid;
        ScrapeCurrentLoot(&entry);
        s_scan.results.push_back(std::move(entry));

        // Loot mode: actually take everything (coin + all slots) before the
        // release below. Scrape ran first, so `GetLastScanResults()` still
        // reports what each corpse held.
        if (s_scan.lootMode)
            LootCurrentWindow();

        // Sends `CMSG_LOOT_RELEASE` and recursively calls this hook
        // with `arg1 == 0`. That recursive call falls into the
        // passthrough branch but still sees `suppressEvents == true`
        // at the FireEvent layer, so the `LOOT_CLOSED` fire inside
        // gets swallowed.
        SendCloseLoot();

        s_scan.suppressEvents = false;
        TryStartNext();
        return;
    }

    s_lootController_o(lootStructPtr, coin, unk);
}

const Game::HookAutoRegister _hookController{
    Offsets::FUN_LOOT_CONTROLLER,
    reinterpret_cast<void *>(&LootController_h),
    reinterpret_cast<void **>(&s_lootController_o)};

const Event::SignalHook::AutoSubscribe _lootIntercept{&SuppressLootEvents};

// === Tick (timeout only) ===

void OnTick() {
    if (s_scan.state == State::Idle)
        return;
    ++s_scan.ticksSinceTransition;
    if (s_scan.ticksSinceTransition >= kStepTimeoutTicks) {
        // Controller hook never fired — the server answered with a
        // failure, or never answered. A failure already cleared the
        // held target; an unanswered request still holds it, which
        // would block all looting, so release it. Skip this corpse.
        s_scan.suppressEvents = false;
        Request::ReleaseIfUnanswered(s_scan.player, s_scan.currentGuid);
        TryStartNext();
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

// === Enumeration ===

struct EnumCtx {
    const void *player;
    std::vector<uint64_t> *out;
};

int __fastcall EnumCallback(EnumCtx *ctx, void * /*unusedEdx*/, uint64_t guid) {
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (obj == nullptr)
        return 1;
    if (!IsLootableUnit(obj))
        return 1;
    if (!InInteractRange(ctx->player, obj))
        return 1;
    ctx->out->push_back(guid);
    return 1;
}

void BuildQueue(void *player, size_t maxCount) {
    s_scan.queue.clear();
    auto Enum = reinterpret_cast<ClntObjMgrEnumVisibleObjects_t>(
        Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
    EnumCtx ctx{player, &s_scan.queue};
    Enum(reinterpret_cast<ClntObjMgrEnumVisibleObjectsCallback_t>(&EnumCallback),
         &ctx);
    // Cap to `maxCount` corpses (0 = unlimited). Order is engine
    // enumeration order, not distance-sorted, so the truncation is
    // arbitrary among in-range corpses.
    if (maxCount != 0 && s_scan.queue.size() > maxCount)
        s_scan.queue.resize(maxCount);
}

// Shared starter for both entry points: resolve the player, build the
// corpse queue, kick off the walk. Returns whether it started — `false` if a
// walk is already in progress, there's no local player, or the player is
// mounted.
bool BeginWalk(bool lootMode, size_t maxCount) {
    if (s_scan.state != State::Idle)
        return false;
    if (*reinterpret_cast<void *volatile *>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr)
        return false;
    void *player = Game::ResolveUnitToken("player");
    if (player == nullptr || Request::IsMounted(player))
        return false;

    s_scan.results.clear();
    s_scan.player = player;
    s_scan.lootMode = lootMode;
    BuildQueue(player, maxCount);

    if (s_scan.queue.empty()) {
        Complete(); // nothing nearby — fire LOOT_SCAN_COMPLETED immediately
        return true;
    }
    TryStartNext();
    return true;
}

// === Lua bindings ===

// `C_Loot.ScanNearbyLoot()` — scrape-only walk. Returns `true` if it
// started (a subsequent `GetLastScanResults()` holds the contents once
// `LOOT_SCAN_COMPLETED` fires), `false` if a walk is already running,
// there's no local player, or the player is mounted.
int __fastcall Script_ScanNearbyLoot(void *L) {
    Game::Lua::PushBool(L, BeginWalk(/*lootMode=*/false, /*maxCount=*/0));
    return 1;
}

// `C_Loot.LootAllCorpses([max])` — loots every nearby lootable corpse
// (coin + all items) in sequence. Optional `max` caps how many corpses are
// visited. Returns `true` if it started, `false` if a walk (scan or loot)
// is already in progress, there's no local player, or the player is
// mounted. `LOOT_SCAN_COMPLETED` fires when the walk finishes;
// `GetLastScanResults()` then reports what each corpse held (i.e. what was
// looted).
int __fastcall Script_LootAllCorpses(void *L) {
    const size_t maxCount = Game::Lua::IsNumber(L, 1)
                                ? static_cast<size_t>(Game::Lua::ToNumber(L, 1))
                                : 0;
    Game::Lua::PushBool(L, BeginWalk(/*lootMode=*/true, maxCount));
    return 1;
}

int __fastcall Script_IsScanInProgress(void *L) {
    Game::Lua::PushBool(L, s_scan.state != State::Idle);
    return 1;
}

int __fastcall Script_GetLastScanResults(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    int outIdx = 1;
    for (const auto &entry : s_scan.results) {
        Game::Lua::PushNumber(L, static_cast<double>(outIdx++));
        Game::Lua::NewTable(L);

        char guidBuf[Guid::STRING_SIZE];
        Game::Lua::SetFieldString(L, "guid",
                                  Guid::FormatAsString(entry.guid, guidBuf,
                                                       sizeof guidBuf));
        Game::Lua::SetFieldNumber(L, "coin", static_cast<double>(entry.coin));

        Game::Lua::PushString(L, "items");
        Game::Lua::NewTable(L);
        int itemIdx = 1;
        for (const auto &item : entry.items) {
            Game::Lua::PushNumber(L, static_cast<double>(itemIdx++));
            Game::Lua::NewTable(L);
            Game::Lua::SetFieldNumber(L, "itemID", static_cast<double>(item.itemID));
            Game::Lua::SetFieldNumber(L, "count", static_cast<double>(item.count));
            if (!item.link.empty())
                Game::Lua::SetFieldString(L, "link", item.link.c_str());
            Game::Lua::SetTable(L, -3);
        }
        Game::Lua::SetTable(L, -3);

        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_Loot", "ScanNearbyLoot",
                                     &Script_ScanNearbyLoot);
    Game::Lua::RegisterTableFunction("C_Loot", "LootAllCorpses",
                                     &Script_LootAllCorpses);
    Game::Lua::RegisterTableFunction("C_Loot", "IsScanInProgress",
                                     &Script_IsScanInProgress);
    Game::Lua::RegisterTableFunction("C_Loot", "GetLastScanResults",
                                     &Script_GetLastScanResults);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Loot::Scan
