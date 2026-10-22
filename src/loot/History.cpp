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

// C_LootHistory backport.
//
// Modern WoW (2.x+, incl. 5.4.8) exposes a `C_LootHistory` namespace backed
// by an engine-maintained ring buffer that a dedicated loot-history server
// packet fills. Vanilla 1.12 has NO such packet or store — but it DOES
// receive the raw group-loot roll traffic (SMSG_LOOT_ROLL per player,
// SMSG_LOOT_ROLL_WON, SMSG_LOOT_ALL_PASSED), which the default UI only turns
// into chat lines. So we reconstruct the history client-side (à la
// Aura::Source): co-hook the three roll-result handlers and accumulate a ring
// of rolled items + per-player results, then expose C_LootHistory reading
// from it.
//
// The handlers run on the deserialized PendingLootRoll message (0x48 bytes,
// LootRoll.cpp) with the fields at the OFF_LOOTROLL_* offsets. The message is
// populated at handler entry and freed at exit, so each co-hook reads its
// fields BEFORE calling the original.
//
// === Handler ABI (verified from disassembly — RET conventions DIFFER) ===
//   ROLL   FUN_0061C0B0: __fastcall(msg=ECX, ctx=EDX, int stackArg) -> RET 4
//   WON    FUN_0061B9E0: __fastcall(ctx=ECX, msg=EDX, int stackArg) -> RET 4
//   PASSED FUN_0061B640: __fastcall(ctx=ECX, msg=EDX)               -> RET 0
// The trailing stack arg (and matching callee cleanup) is LOAD-BEARING: an
// earlier revision declared these as 2-arg hooks (implicit RET 0), so every
// ROLL/WON call left ESP unbalanced by 4 -> stack corruption -> crash on the
// first loot roll. Each hook below matches its handler's exact shape.
//
// Name + class per roller come from the engine NameCache (read at hook time,
// guaranteed loaded then), so they resolve even for someone who has left the
// group by the time their roll packet arrives. Surface: C_LootHistory
// GetNumItems / GetItem / GetPlayerInfo, plus LOOT_HISTORY_ROLL_CHANGED /
// _ROLL_COMPLETE events. The MoP master-loot management functions
// (SetExpiration / GiveMasterLoot / CanMasterLoot) are intentionally omitted
// — they depend on server support 1.12 doesn't have.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"
#include "event/Custom.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>

namespace Loot::History {

namespace {

template <typename T>
T Read(const void *base, int off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off);
}

// Modern rollType constants (what C_LootHistory.GetPlayerInfo returns).
constexpr int ROLL_PASS = 0;
constexpr int ROLL_NEED = 1;
constexpr int ROLL_GREED = 2;

// A dice result's type: 2 = greed, anything else = need. The same rule
// WriteRollLine (FUN_0061C0B0) uses for its ROLLED_* lines and
// WriteRollWonLine (FUN_0061B9E0) for its _NO_SPAM_* lines.
int DiceRollType(int packetType) {
    return packetType == 2 ? ROLL_GREED : ROLL_NEED;
}

// GUID -> engine NameCache entry-data block. This is the same primitive the
// engine's own `%N` name substitution uses (FUN_005083A0): for a LOADED entry
// the lookup returns the data block (name[48] at +0, class at +0x140, …),
// regardless of the (null) callback and regardless of current group
// membership — so it resolves a roller who has since left the group, which
// is exactly what a loot roll needs (the roll packet arrives after they may
// have gone). Guaranteed loaded at hook time: the roll handler's wrapper
// gates on this same lookup succeeding. Returns nullptr if not cached.
const uint8_t *LookupPlayerRecord(uint64_t guid) {
    return Unit::Identity::PlayerInfoRecord(guid);
}

const char *NameFromRecord(const uint8_t *rec) {
    return rec != nullptr
               ? Game::Ptr<const char>(rec, Offsets::OFF_PLAYER_INFO_NAME)
               : nullptr;
}

uint32_t ClassIdFromRecord(const uint8_t *rec) {
    return rec != nullptr
               ? Game::Read<uint32_t>(rec, Offsets::OFF_PLAYER_INFO_CLASS)
               : 0;
}

constexpr int kNameLen = 48; // engine NameCache inline name[48]

void CopyName(char *dst, const char *src) {
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    int i = 0;
    for (; i < kNameLen - 1 && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

struct PlayerRoll {
    uint64_t guid;
    char name[kNameLen];
    uint32_t classId; // ChrClasses.dbc ID (0 = unknown)
    int rollType;     // ROLL_* (modern); ROLL_PASS when the player passed
    int roll;         // 1..100 dice result, 0 = no dice rolled (a vote / pass)
};

constexpr int kMaxPlayers = 40;

struct HistItem {
    uint32_t rollID;     // stable monotonic ID — survives ring-index shifts
    uint64_t sourceGuid; // looted-object GUID — the roll's identity
    uint32_t slot;
    uint32_t itemId;
    int32_t itemSuffix;  // random suffix/property ID (link suffix field)
    uint32_t itemSeed;   // random seed (link unique field)
    uint64_t winnerGuid; // winner's roller GUID (0 if undecided)
    bool hasWinner;
    bool isDone;
    PlayerRoll players[kMaxPlayers];
    int playerCount;
    bool used;
};

// Ring of recent rolled items. g_write is the next slot to fill; g_count
// saturates at kMaxItems. GetNumItems returns g_count; item index i (1-based,
// oldest→newest) maps to ring[(g_write - g_count + i - 1) mod kMaxItems].
// ~3 KB per item (players[40] dominates), so 128 is ~380 KB static — bump
// freely if a longer history is wanted.
constexpr int kMaxItems = 128;
HistItem g_items[kMaxItems];
int g_write = 0;
int g_count = 0;
uint32_t g_nextRollId = 1; // 0 is reserved as "invalid" (never assigned)

// Set by FindOrCreate for the most recent call, read by the hooks to decide
// whether a structural LOOT_HISTORY_FULL_UPDATE is warranted (a new item
// appeared, or the ring evicted an old one so every index shifted).
bool g_createdNew = false;
bool g_evicted = false;

// 1-based access in oldest→newest order (matching GetNumItems' count).
HistItem *GetEntry(int index1Based) {
    if (index1Based < 1 || index1Based > g_count)
        return nullptr;
    const int start = (g_write - g_count + kMaxItems) % kMaxItems;
    return &g_items[(start + index1Based - 1) % kMaxItems];
}

// The newest entry for (sourceGuid, slot). Creature and chest GUIDs repeat
// across respawns, so an earlier, finished roll on the same slot can still be
// in the ring; the newest one is the roll the packet belongs to.
HistItem *Find(uint64_t sourceGuid, uint32_t slot) {
    for (int i = g_count; i >= 1; --i) {
        HistItem *e = GetEntry(i);
        if (e->sourceGuid == sourceGuid && e->slot == slot)
            return e;
    }
    return nullptr;
}

// Append a fresh entry at the ring head, evicting the oldest when full.
HistItem *Create(uint64_t sourceGuid, uint32_t slot, uint32_t itemId) {
    HistItem *e = &g_items[g_write];
    g_evicted = e->used; // overwriting a live slot => ring full => eviction
    g_write = (g_write + 1) % kMaxItems;
    if (g_count < kMaxItems)
        ++g_count;
    *e = HistItem{};
    e->used = true;
    e->rollID = g_nextRollId++;
    e->sourceGuid = sourceGuid;
    e->slot = slot;
    e->itemId = itemId;
    g_createdNew = true;
    return e;
}

HistItem *FindOrCreate(uint64_t sourceGuid, uint32_t slot, uint32_t itemId) {
    g_createdNew = false;
    g_evicted = false;
    HistItem *e = Find(sourceGuid, slot);
    if (e == nullptr)
        return Create(sourceGuid, slot, itemId);
    if (itemId != 0)
        e->itemId = itemId;
    return e;
}

// Find the player-roll slot for `guid`, appending a fresh one (name/class
// resolved from the NameCache) if absent and there's room. Returns the
// 0-based index, or -1 when the roster is full. Shared by the roll recorder
// and the winner recorder — the winner may never have sent an individual
// SMSG_LOOT_ROLL (joined late / master loot), so ensuring they're present
// lets GetItem's winnerIdx resolve through GetPlayerInfo.
int FindOrAddPlayer(HistItem *e, uint64_t guid) {
    for (int i = 0; i < e->playerCount; ++i) {
        if (e->players[i].guid == guid)
            return i;
    }
    if (e->playerCount >= kMaxPlayers)
        return -1;
    const int idx = e->playerCount++;
    const uint8_t *rec = LookupPlayerRecord(guid); // cache loaded at hook time
    e->players[idx] = PlayerRoll{};
    e->players[idx].guid = guid;
    CopyName(e->players[idx].name, NameFromRecord(rec));
    e->players[idx].classId = ClassIdFromRecord(rec);
    return idx;
}

// 1-based index within players[] of the winner, or 0 if undecided / absent.
int WinnerIndex(const HistItem *e) {
    if (!e->hasWinner || e->winnerGuid == 0)
        return 0;
    for (int i = 0; i < e->playerCount; ++i) {
        if (e->players[i].guid == e->winnerGuid)
            return i + 1;
    }
    return 0;
}

// The 1-based index of an entry in oldest→newest order (0 if not live).
int IndexOf(const HistItem *e) {
    const int start = (g_write - g_count + kMaxItems) % kMaxItems;
    for (int i = 0; i < g_count; ++i) {
        if (&g_items[(start + i) % kMaxItems] == e)
            return i + 1;
    }
    return 0;
}

// Modern loot-history events. ROLL_CHANGED(itemIdx, playerIdx) fires on each
// roll; ROLL_COMPLETE(itemIdx) when the item is decided (winner / all-passed).
// FULL_UPDATE is reserved for API/IsEventValid completeness (the UI fires it
// on open); we don't dispatch it from C.
constexpr const char *kRollChanged = "LOOT_HISTORY_ROLL_CHANGED";
constexpr const char *kRollComplete = "LOOT_HISTORY_ROLL_COMPLETE";
constexpr const char *kFullUpdate = "LOOT_HISTORY_FULL_UPDATE";
const Event::Custom::AutoReserve _evRollChanged{kRollChanged};
const Event::Custom::AutoReserve _evRollComplete{kRollComplete};
const Event::Custom::AutoReserve _evFullUpdate{kFullUpdate};

// Returns the 1-based item index and writes the 1-based player index to
// `*outPlayerIdx` (0 if none), so the hook can fire ROLL_CHANGED.
int RecordRoll(const void *msg, int *outPlayerIdx) {
    *outPlayerIdx = 0;
    const uint64_t src = Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_SOURCE_GUID);
    const uint32_t slot = Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_SLOT);
    const uint32_t itemId = Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_ITEM_ID);
    const uint64_t roller = Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_ROLLER_GUID);
    const uint8_t rollByte = Read<uint8_t>(msg, Offsets::OFF_LOOTROLL_ROLL_NUM);
    const int packetType = Read<int32_t>(msg, Offsets::OFF_LOOTROLL_ROLL_TYPE);

    // WriteRollLine's (FUN_0061C0B0) four cases: a negative byte is a vote,
    // greed for type 2 else pass; a non-negative byte with type 0 is a need
    // vote; anything else is a dice result carrying the number.
    int rollType;
    int rollNum = 0;
    if (static_cast<int8_t>(rollByte) < 0) {
        rollType = packetType == 2 ? ROLL_GREED : ROLL_PASS;
    } else if (packetType == 0) {
        rollType = ROLL_NEED;
    } else {
        rollType = DiceRollType(packetType);
        rollNum = rollByte;
    }

    HistItem *e = FindOrCreate(src, slot, itemId);
    if (roller == 0)
        return IndexOf(e);

    const int slotIdx = FindOrAddPlayer(e, roller);
    if (slotIdx >= 0) {
        e->players[slotIdx].rollType = rollType;
        e->players[slotIdx].roll = rollNum;
        *outPlayerIdx = slotIdx + 1;
    }
    return IndexOf(e);
}

int RecordWinner(const void *msg) {
    const uint64_t src = Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_SOURCE_GUID);
    const uint32_t slot = Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_SLOT);
    const uint32_t itemId = Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_ITEM_ID);
    const uint64_t winner = Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_ROLLER_GUID);

    HistItem *e = FindOrCreate(src, slot, itemId);
    e->winnerGuid = winner;
    e->hasWinner = true;
    e->isDone = true;
    if (winner != 0) {
        // Add the winner so GetItem's winnerIdx resolves, and take their roll
        // from this message (HandleLootRollWon FUN_0061B870 stores it in the
        // same fields), in case their own SMSG_LOOT_ROLL never arrived.
        const int idx = FindOrAddPlayer(e, winner);
        if (idx >= 0) {
            e->players[idx].rollType =
                DiceRollType(Read<int32_t>(msg, Offsets::OFF_LOOTROLL_ROLL_TYPE));
            e->players[idx].roll = Read<uint8_t>(msg, Offsets::OFF_LOOTROLL_ROLL_NUM);
        }
    }
    return IndexOf(e);
}

int RecordAllPassed(const void *msg) {
    const uint64_t src = Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_SOURCE_GUID);
    const uint32_t slot = Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_SLOT);
    const uint32_t itemId = Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_ITEM_ID);

    HistItem *e = FindOrCreate(src, slot, itemId);
    e->isDone = true;
    return IndexOf(e);
}

// SMSG_LOOT_START_ROLL carries the item's random suffix/seed; the result
// packets don't. It's read from the freshly-built store-head node (rather
// than the packet) right after the START_ROLL handler runs — the node has
// the same field offsets as the message. This is the only place the random
// fields are populated, so the result recorders leave them alone.
//
// A START_ROLL always opens a new entry, as the engine always allocates a new
// record for it: the same GUID + slot can roll again after a respawn. Returns
// whether an entry was added.
bool RecordStart() {
    const uint8_t *node = Game::Read<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_LOOTROLL_STORE_HEAD));
    if (node == nullptr)
        return false;
    const uint64_t src = Read<uint64_t>(node, Offsets::OFF_LOOTROLL_SOURCE_GUID);
    const uint32_t slot = Read<uint32_t>(node, Offsets::OFF_LOOTROLL_SLOT);
    const uint32_t itemId = Read<uint32_t>(node, Offsets::OFF_LOOTROLL_ITEM_ID);
    HistItem *e = Create(src, slot, itemId);
    e->itemSuffix = Read<int32_t>(node, Offsets::OFF_LOOTROLL_ITEM_SUFFIX);
    e->itemSeed = Read<uint32_t>(node, Offsets::OFF_LOOTROLL_ITEM_SEED);
    return true;
}

// ---- Packet co-hooks -------------------------------------------------------
// Signatures match the verified per-handler ABI above (note the differing
// message register, stack arg, and RET). Read the message fields before the
// original runs — it frees the message.

using RollFn_t = void(__fastcall *)(void *msg, void *ctx, int a3);   // RET 4
using WonFn_t = void(__fastcall *)(void *ctx, void *msg, int a3);    // RET 4
using PassedFn_t = void(__fastcall *)(void *ctx, void *msg);         // RET 0
using StartFn_t = int(__fastcall *)(void *packet);                  // RET 0, returns 1

RollFn_t g_origRoll = nullptr;
WonFn_t g_origWon = nullptr;
PassedFn_t g_origPassed = nullptr;
StartFn_t g_origStart = nullptr;

// Fire LOOT_HISTORY_FULL_UPDATE (no payload) — the "rebuild everything"
// signal, matching the UI's full refresh. Called when the item set changes
// structurally: a new item appears or the ring evicts an old one (shifting
// every index).
void FireFullUpdate() {
    Event::Custom::Fire(_evFullUpdate.Slot(), "");
}

// START_ROLL: let the original build the store node, then read the item's
// random suffix off it. Adds the item to the history so a UI can show it as
// soon as the roll opens (before any roll comes in) — hence the FULL_UPDATE.
int __fastcall Start_h(void *packet) {
    const int handled = g_origStart(packet);
    if (RecordStart())
        FireFullUpdate();
    return handled;
}

// Record fields (message is freed by the original), run the original, THEN
// fire the event — firing after the original keeps the engine's own handler
// off our re-entrancy (same order Item::Data uses from its packet hook). A
// structural FULL_UPDATE (if any) precedes the granular event so a listener
// that rebuilds on FULL_UPDATE sees the new row before the row-level refresh.
void __fastcall Roll_h(void *msg, void *ctx, int a3) {
    int itemIdx = 0, playerIdx = 0;
    bool structural = false;
    if (msg != nullptr) {
        itemIdx = RecordRoll(msg, &playerIdx);
        structural = g_createdNew || g_evicted;
    }
    g_origRoll(msg, ctx, a3);
    if (structural)
        FireFullUpdate();
    if (itemIdx > 0)
        Event::Custom::Fire(_evRollChanged.Slot(), "%d%d", itemIdx, playerIdx);
}

void __fastcall Won_h(void *ctx, void *msg, int a3) {
    int itemIdx = 0;
    bool structural = false;
    if (msg != nullptr) {
        itemIdx = RecordWinner(msg);
        structural = g_createdNew || g_evicted;
    }
    g_origWon(ctx, msg, a3);
    if (structural)
        FireFullUpdate();
    if (itemIdx > 0)
        Event::Custom::Fire(_evRollComplete.Slot(), "%d", itemIdx);
}

void __fastcall Passed_h(void *ctx, void *msg) {
    int itemIdx = 0;
    bool structural = false;
    if (msg != nullptr) {
        itemIdx = RecordAllPassed(msg);
        structural = g_createdNew || g_evicted;
    }
    g_origPassed(ctx, msg);
    if (structural)
        FireFullUpdate();
    if (itemIdx > 0)
        Event::Custom::Fire(_evRollComplete.Slot(), "%d", itemIdx);
}

const Game::HookAutoRegister _hookRoll{
    Offsets::FUN_LOOT_ROLL_HANDLER, reinterpret_cast<void *>(&Roll_h),
    reinterpret_cast<void **>(&g_origRoll)};
const Game::HookAutoRegister _hookWon{
    Offsets::FUN_LOOT_ROLL_WON_HANDLER, reinterpret_cast<void *>(&Won_h),
    reinterpret_cast<void **>(&g_origWon)};
const Game::HookAutoRegister _hookPassed{
    Offsets::FUN_LOOT_ALL_PASSED_HANDLER, reinterpret_cast<void *>(&Passed_h),
    reinterpret_cast<void **>(&g_origPassed)};
const Game::HookAutoRegister _hookStart{
    Offsets::FUN_LOOT_START_ROLL_HANDLER, reinterpret_cast<void *>(&Start_h),
    reinterpret_cast<void **>(&g_origStart)};

// ---- Lua surface -----------------------------------------------------------

// `C_LootHistory.GetNumItems()` -> number of rolled items in the history.
int __fastcall Script_GetNumItems(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(g_count));
    return 1;
}

// `C_LootHistory.GetItem(itemIndex)` ->
//   rollID, itemLink, numPlayers, isDone, winnerIdx, isMasterLoot, isCurrency
// Matches 5.4.8: `rollID` is a stable ID (unlike itemIndex, which shifts as
// the ring saturates — key expanded/highlighted rows on it); `itemLink` is the
// `item:id:enchant:suffix:unique` form (valid for GetItemInfo / SetHyperlink),
// carrying the roll's random suffix so "of the X" items keep their affix;
// `winnerIdx` is the winner's 1-based playerIndex for
// GetPlayerInfo(itemIndex, winnerIdx) (nil if undecided). Every entry comes
// from a group-loot roll on an item, so isMasterLoot and isCurrency are false.
int __fastcall Script_GetItem(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetItem(itemIndex)");
        return 0;
    }
    HistItem *e = GetEntry(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (e == nullptr)
        return 0;
    static char link[64];
    // Vanilla item link: item:id:enchant:suffix:unique. Carry the roll's
    // random suffix/seed so "of the Bear"-style items keep their affix.
    std::snprintf(link, sizeof(link), "item:%u:0:%d:%u", e->itemId,
                  e->itemSuffix, e->itemSeed);
    Game::Lua::PushNumber(L, static_cast<double>(e->rollID));       // 1 rollID
    Game::Lua::PushString(L, link);                                 // 2 itemLink
    Game::Lua::PushNumber(L, static_cast<double>(e->playerCount));  // 3 numPlayers
    Game::Lua::PushBool(L, e->isDone);                              // 4 isDone
    const int winnerIdx = WinnerIndex(e);
    if (winnerIdx > 0)
        Game::Lua::PushNumber(L, static_cast<double>(winnerIdx));   // 5 winnerIdx
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushBool(L, false);                                  // 6 isMasterLoot
    Game::Lua::PushBool(L, false);                                  // 7 isCurrency
    return 7;
}

// `C_LootHistory.GetPlayerInfo(itemIndex, playerIndex)` ->
// name, classToken, rollType, roll, isWinner, isMe. classToken is the
// ChrClasses filename ("WARRIOR", …) for RAID_CLASS_COLORS-style coloring;
// both name and class come from the engine NameCache captured at roll time.
// roll is nil until a dice result arrives (5.4.8's UI tests it for truth, so
// a 0 would show as "0" and keep passers listed). isMe compares the roller
// GUID to the local player's.
int __fastcall Script_GetPlayerInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GetPlayerInfo(itemIndex, playerIndex)");
        return 0;
    }
    HistItem *e = GetEntry(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (e == nullptr)
        return 0;
    const int p = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;
    if (p < 0 || p >= e->playerCount)
        return 0;
    const PlayerRoll &r = e->players[p];
    if (r.name[0] != '\0')
        Game::Lua::PushString(L, r.name);                    // 1 name
    else
        Game::Lua::PushNil(L);
    const char *classToken = DBC::ClassToken(r.classId);     // 2 class token
    if (classToken != nullptr)
        Game::Lua::PushString(L, classToken);
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushNumber(L, static_cast<double>(r.rollType)); // 3 rollType
    if (r.roll != 0)
        Game::Lua::PushNumber(L, static_cast<double>(r.roll)); // 4 roll
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushBool(L, e->hasWinner && e->winnerGuid == r.guid); // 5 isWinner
    Game::Lua::PushBool(L, r.guid == Unit::Identity::PlayerGuid());  // 6 isMe
    return 6;
}

// `C_LootHistory.Clear()` -> wipe the accumulated history. A ClassicAPI
// extension (no MoP equivalent) so a UI can offer a "clear history" button.
// Fires LOOT_HISTORY_FULL_UPDATE so any open display rebuilds empty.
int __fastcall Script_Clear(void *L) {
    (void)L;
    for (auto &e : g_items)
        e.used = false;
    g_write = 0;
    g_count = 0;
    FireFullUpdate();
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_LootHistory", "GetNumItems",
                                     &Script_GetNumItems);
    Game::Lua::RegisterTableFunction("C_LootHistory", "GetItem",
                                     &Script_GetItem);
    Game::Lua::RegisterTableFunction("C_LootHistory", "GetPlayerInfo",
                                     &Script_GetPlayerInfo);
    Game::Lua::RegisterTableFunction("C_LootHistory", "Clear", &Script_Clear);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Loot::History
