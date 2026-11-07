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
//
// Loot mode remembers corpses whose window opened EMPTY but which stay
// LOOTABLE: the server keeps the flag while any loot is left (another
// member's quest item, condition-gated loot) yet never sends this player a
// slot it can't take, so an empty window is the server's own "nothing here
// for you". Later walks skip such a corpse. A corpse whose window held
// anything (bags full, a roll or master-loot assignment in progress) is not
// remembered and is retried, because those refusals clear on their own.
// See `s_emptyCorpses` for when an entry is forgotten.

#include "../Game.h"
#include "../Offsets.h"
#include "../event/Custom.h"
#include "../event/SignalHook.h"
#include "../guid/Guid.h"
#include "../object/Resolve.h"
#include "../tick/WorldTick.h"
#include "../time/Clock.h"
#include "Request.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Loot::Scan {

namespace {

constexpr const char *kEventCompleted = "LOOT_SCAN_COMPLETED";
const Event::Custom::AutoReserve _reserve{kEventCompleted};

// How long one step may wait: for the server to answer a CMSG_LOOT it
// never answers, or for the engine to accept a send it keeps refusing
// (the player is moving). Long enough for high-latency servers, short
// enough that a dropped response doesn't hang the walk.
constexpr uint32_t kStepTimeoutMs = 3000;

// Event IDs the engine fires through `FUN_FIRE_EVENT_NO_ARGS` for
// loot-window transitions. The hook swallows these only when the
// scanner has the suppression flag raised — normal play sees them
// fire unmodified.
constexpr int EVENT_LOOT_OPENED = 0x10B;
constexpr int EVENT_LOOT_CLOSED = 0x10D;
// Observed, never swallowed (see `s_emptyCorpses`).
constexpr int EVENT_QUEST_LOG_UPDATE = 0x134;

enum class State {
    Idle,
    WaitingToSend,      // the engine refused the send for now; retried each tick
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
    uint32_t stepStartMs = 0;
    bool suppressEvents = false; // read by the FireEvent hook
    bool lootMode = false;       // LootAllCorpses (true) vs ScanNearbyLoot (false)
    std::vector<uint64_t> queue;
    std::vector<LootEntry> results;
};

ScanCtx s_scan;

// Loot-mode corpses whose window opened empty while the unit stayed
// LOOTABLE (see the file comment). Outlives a walk. An entry is forgotten:
//   - on the first world tick where its unit is gone (despawned, out of
//     view) or no longer LOOTABLE (alive again after a respawn
//     with the same GUID, or emptied by someone else), so a corpse back in
//     view or a fresh corpse is opened again;
//   - all at once on QUEST_LOG_UPDATE: quest items are per-player loot, so
//     a quest change can turn an empty window into one with an item in it.
std::vector<uint64_t> s_emptyCorpses;

bool IsKnownEmpty(uint64_t guid) {
    return std::find(s_emptyCorpses.begin(), s_emptyCorpses.end(), guid) !=
           s_emptyCorpses.end();
}

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

// Run every world tick, so no gone-and-back or respawn-and-die-again
// can happen between two checks.
void PruneEmptyCorpses() {
    s_emptyCorpses.erase(
        std::remove_if(s_emptyCorpses.begin(), s_emptyCorpses.end(),
                       [](uint64_t guid) {
                           void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT,
                                                       guid, nullptr, 0);
                           return unit == nullptr || !Request::IsLootable(unit);
                       }),
        s_emptyCorpses.end());
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

void BeginStep(State state) {
    s_scan.state = state;
    s_scan.stepStartMs = Time::Clock::NowMs();
}

// Sends the current corpse's loot request. The corpse is checked again
// here, not only when the queue was built: the walk takes a round trip per
// corpse, and by now the corpse may be gone, looted, or out of reach
// because the player moved. The server refuses an out-of-reach corpse
// with an error, so skip it instead.
//
// When the engine refuses the send itself (the player is moving or
// falling, ...), the corpse stays current and `OnTick` retries it: the
// refusal is about the player, not the corpse, so skipping would drop
// every corpse left in the queue.
void SendCurrent() {
    void *target =
        Object::ByGuid(Offsets::TYPEMASK_UNIT, s_scan.currentGuid, nullptr, 0);
    if (target == nullptr || !Request::IsLootable(target) ||
        !Request::InReach(s_scan.player, target)) {
        TryStartNext();
        return;
    }
    if (Request::Send(s_scan.player, target, s_scan.currentGuid))
        BeginStep(State::WaitingForResponse);
}

void StartLoot(uint64_t guid) {
    s_scan.currentGuid = guid;
    BeginStep(State::WaitingToSend);
    SendCurrent();
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
        // Each corpse is queued once per walk and known-empty ones are not
        // queued in loot mode, so this never adds a duplicate.
        if (s_scan.lootMode && entry.coin == 0 && entry.items.empty())
            s_emptyCorpses.push_back(entry.guid);
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

// Observes only: the event still fires.
bool ForgetEmptyCorpsesOnQuestChange(int eventID) {
    if (eventID == EVENT_QUEST_LOG_UPDATE)
        s_emptyCorpses.clear();
    return false;
}

const Event::SignalHook::AutoSubscribe _questIntercept{&ForgetEmptyCorpsesOnQuestChange};

// === Tick (memory upkeep, refusals and timeouts) ===

void OnTick() {
    PruneEmptyCorpses();
    if (s_scan.state == State::Idle)
        return;
    const bool timedOut = Time::Clock::Elapsed(s_scan.stepStartMs) >= kStepTimeoutMs;

    if (s_scan.state == State::WaitingToSend) {
        // Still refused: the player has not stopped moving, or mounted,
        // or lost control. Every corpse left would be refused the same
        // way, so end the walk.
        if (timedOut) {
            Complete();
            return;
        }
        SendCurrent();
        return;
    }

    // The server refused the corpse ("too far away", not ours, ...): its
    // failure response and its release response both clear the held
    // target, and no window opens. Go on to the next corpse now.
    if (Request::HeldTarget(s_scan.player) != s_scan.currentGuid) {
        TryStartNext();
        return;
    }
    if (timedOut) {
        // No answer at all. The held target would block all looting, so
        // release it, and skip this corpse.
        Request::ReleaseIfUnanswered(s_scan.player, s_scan.currentGuid);
        TryStartNext();
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

// === Enumeration ===

struct EnumCtx {
    void *player;
    std::vector<uint64_t> *out;
    bool skipKnownEmpty;
};

int __fastcall EnumCallback(EnumCtx *ctx, void * /*unusedEdx*/, uint64_t guid) {
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (obj == nullptr)
        return 1;
    if (!Request::IsLootable(obj))
        return 1;
    if (ctx->skipKnownEmpty && IsKnownEmpty(guid))
        return 1;
    if (!Request::InReach(ctx->player, obj))
        return 1;
    ctx->out->push_back(guid);
    return 1;
}

void BuildQueue(void *player, size_t maxCount, bool skipKnownEmpty) {
    s_scan.queue.clear();
    auto Enum = reinterpret_cast<ClntObjMgrEnumVisibleObjects_t>(
        Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
    EnumCtx ctx{player, &s_scan.queue, skipKnownEmpty};
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
    BuildQueue(player, maxCount, /*skipKnownEmpty=*/lootMode);

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
