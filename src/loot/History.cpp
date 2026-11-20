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

// C_LootHistory, built the way the 3.80.2 Classic client keeps it.
//
// 1.12 has no loot-history store, but it receives the group-loot roll
// traffic (SMSG_LOOT_START_ROLL, SMSG_LOOT_ROLL per player,
// SMSG_LOOT_ROLL_WON, SMSG_LOOT_ALL_PASSED), which the stock UI only turns
// into chat lines. We co-hook the four handlers and keep the store the
// 3.80.2 client keeps (read from WowClassicGM-China.exe):
//
// - Only a roll start adds an item (0x141A90AA0, from START_LOOT_ROLL), and
//   only while you are in a party or raid. An item already in the store for
//   the same loot object and slot is deleted first. The new item goes to the
//   front (index 1 = newest), fires FULL_UPDATE, then the store is trimmed.
// - Trim (0x141A90D00): while there are more than numItemsToSave items and
//   the oldest is at least secondsToSave old, drop it; one FULL_UPDATE if any
//   went. It runs only on an add and on SetExpiration — there is no timer.
//   The defaults are 10 items and 600 s (0x14168D90E).
// - At a roll start the item gets a row for every group member, in group
//   order, with no vote (0x141A900F0). The roll packet (0x14153AAF0 ->
//   0x141A90F30) finds the row by GUID and stores the vote or the dice
//   result; a GUID with no row changes nothing. Each one fires
//   ROLL_CHANGED(item, player).
// - The result (0x141A915E0; ROLL_WON, ALL_PASSED) is ignored once a winner
//   is set. It marks the item done, sets the winner and sorts the rows
//   (0x141A920F0): the winner, then need > greed > pass, then the higher
//   roll, then group order. The winner's row is not written from the won
//   packet. Then ROLL_COMPLETE.
// - Name and class are looked up when GetPlayerInfo is called (name cache
//   0x14136B900), not stored with the row.
// - World teardown (0x141693A90) empties the store with no event.
//
// - After the add, AUTO_SHOW(rollID, isMasterLoot) for a bind-on-pickup item
//   (0x141539B10).
//
// Kept from before: ROLL_COMPLETE carries the item index (3.80.2 sends no
// payload), and C_LootHistory.Clear is our extension.
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

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"
#include "event/Custom.h"
#include "group/Roster.h"
#include "item/Data.h"
#include "item/Link.h"
#include "item/Record.h"
#include "player/FullName.h"
#include "time/Clock.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Loot::History {

namespace {

template <typename T>
T Read(const void *base, int off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off);
}

// rollType values (what C_LootHistory.GetPlayerInfo returns).
constexpr int ROLL_PASS = 0;
constexpr int ROLL_NEED = 1;
constexpr int ROLL_GREED = 2;

// A dice result's type: 2 = greed, anything else = need. The same rule
// WriteRollLine (FUN_0061C0B0) uses for its ROLLED_* lines.
int DiceRollType(int packetType) {
    return packetType == 2 ? ROLL_GREED : ROLL_NEED;
}

// 3.80.2's group array cap (0x28).
constexpr int kMaxPlayers = 40;

struct PlayerRoll {
    uint64_t guid;
    int order;    // group order at the roll start, the last sort key
    int rollType; // ROLL_*; 0 until the player votes
    int roll;     // dice result, 0 = none
    bool voted;
};

struct HistItem {
    uint32_t rollID; // the loot roll's own id (START_LOOT_ROLL's rollID)
    uint64_t sourceGuid;
    uint32_t slot;
    uint32_t itemId;
    int32_t itemSuffix; // random suffix / property ID (link suffix field)
    uint32_t itemSeed;  // random seed (link unique field)
    uint32_t addedMs;
    uint64_t winnerGuid;
    int winnerIdx; // 0-based row of the winner after the sort, -1 for none
    bool isDone;
    bool awaitingItem; // added while the item was not cached
    PlayerRoll players[kMaxPlayers];
    int playerCount;
};

// Front = newest = index 1.
std::deque<HistItem> g_items;

int g_numItemsToSave = 10;
int g_msToSave = 600000;

constexpr const char *kRollChanged = "LOOT_HISTORY_ROLL_CHANGED";
constexpr const char *kRollComplete = "LOOT_HISTORY_ROLL_COMPLETE";
constexpr const char *kFullUpdate = "LOOT_HISTORY_FULL_UPDATE";
const Game::Doc::Field kRollChangedPayload[] = {
    Game::Doc::Req("historyIndex", "luaIndex"),
    Game::Doc::Req("playerIndex", "luaIndex"),
};
const Game::Doc::Event kRollChangedDoc{
    "Loot", "Fires when a player selects need, greed or pass on a loot roll, or rolls the dice.",
    kRollChangedPayload};

const Game::Doc::Field kRollCompletePayload[] = {
    Game::Doc::Req("historyIndex", "luaIndex"),
};
const Game::Doc::Event kRollCompleteDoc{
    "Loot", "Fires when a loot roll is decided, by a winner or because every player passed.",
    kRollCompletePayload};

const Game::Doc::Event kFullUpdateDoc{
    "Loot",
    "Fires when an item is added to or dropped from the loot history, when the data of an item "
    "arrives, or when the history is cleared, so every index may have changed.",
    {}};

constexpr const char *kAutoShow = "LOOT_HISTORY_AUTO_SHOW";
const Game::Doc::Field kAutoShowPayload[] = {
    Game::Doc::Req("rollID", "number"),
    Game::Doc::Opt("isMasterLoot", "bool", nullptr, "Always nil."),
};
const Game::Doc::Event kAutoShowDoc{
    "Loot", "Fires when a loot roll starts on an item that binds when picked up.",
    kAutoShowPayload};

const Event::Custom::AutoReserve _evRollChanged{kRollChanged, &kRollChangedDoc};
const Event::Custom::AutoReserve _evRollComplete{kRollComplete, &kRollCompleteDoc};
const Event::Custom::AutoReserve _evFullUpdate{kFullUpdate, &kFullUpdateDoc};
const Event::Custom::AutoReserve _evAutoShow{kAutoShow, &kAutoShowDoc};

void FireFullUpdate() {
    Event::Custom::Fire(_evFullUpdate.Slot(), "");
}

// 0x141539B10: AUTO_SHOW(rollID, isMasterLoot) for a currency or an item
// whose Bonding byte (+0x1C9) is 1, after the item is added — byte compare at
// 0x141539CDB. 1.12 keeps Bonding at OFF_ITEMSTATS_BONDING, the field its own
// vote tests before CONFIRM_LOOT_ROLL. 1.12 rolls no master-loot items, so
// isMasterLoot is false (nil).
void FireAutoShowIfBoP(uint32_t rollID, const uint8_t *record) {
    if (record != nullptr &&
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_BONDING) == 1)
        Event::Custom::Fire(_evAutoShow.Slot(), "%u%s", rollID,
                            static_cast<const char *>(nullptr));
}

// 1-based access, newest first.
HistItem *GetEntry(int index1Based) {
    if (index1Based < 1 || index1Based > static_cast<int>(g_items.size()))
        return nullptr;
    return &g_items[static_cast<size_t>(index1Based - 1)];
}

// The 1-based index of the item for (loot object, slot), or 0.
int Find(uint64_t sourceGuid, uint32_t slot) {
    for (size_t i = 0; i < g_items.size(); ++i) {
        if (g_items[i].sourceGuid == sourceGuid && g_items[i].slot == slot)
            return static_cast<int>(i) + 1;
    }
    return 0;
}

// 0x141A90D00.
void Trim() {
    const uint32_t now = Time::Clock::NowMs();
    bool removed = false;
    while (static_cast<int64_t>(g_items.size()) > g_numItemsToSave &&
           static_cast<int64_t>(Time::Clock::Elapsed(g_items.back().addedMs, now)) >=
               g_msToSave) {
        g_items.pop_back();
        removed = true;
    }
    if (removed)
        FireFullUpdate();
}

// The group at the roll start, in group order: the raid roster, or you and
// your party. The 3.80.2 client copies its group's member list, which holds
// you; 1.12's party list does not, so you go first.
int CollectGroup(PlayerRoll *out) {
    int n = 0;
    auto add = [&](uint64_t guid) {
        if (guid == 0 || n >= kMaxPlayers)
            return;
        out[n] = PlayerRoll{};
        out[n].guid = guid;
        out[n].order = n;
        ++n;
    };
    if (const int raid = Group::Roster::RaidCount(); raid > 0) {
        for (int i = 0; i < raid; ++i)
            add(Group::Roster::RaidGuid(i));
    } else if (Group::Roster::PartyCount() > 0) {
        add(Unit::Identity::PlayerGuid());
        for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i)
            add(Group::Roster::PartyGuid(i));
    }
    return n;
}

// SMSG_LOOT_START_ROLL carries the item's random suffix / seed and the roll
// id; it is read from the freshly-built store-head record right after the
// handler runs. 0x141A90AA0. Returns whether an item was added.
bool Add() {
    const uint8_t *node = Game::Read<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_LOOTROLL_STORE_HEAD));
    if (node == nullptr)
        return false;

    HistItem e{};
    e.playerCount = CollectGroup(e.players);
    if (e.playerCount == 0) // not in a group
        return false;

    e.rollID = Read<uint32_t>(node, Offsets::OFF_LOOTROLL_ID);
    e.sourceGuid = Read<uint64_t>(node, Offsets::OFF_LOOTROLL_SOURCE_GUID);
    e.slot = Read<uint32_t>(node, Offsets::OFF_LOOTROLL_SLOT);
    e.itemId = Read<uint32_t>(node, Offsets::OFF_LOOTROLL_ITEM_ID);
    e.itemSuffix = Read<int32_t>(node, Offsets::OFF_LOOTROLL_ITEM_SUFFIX);
    e.itemSeed = Read<uint32_t>(node, Offsets::OFF_LOOTROLL_ITEM_SEED);
    e.addedMs = Time::Clock::NowMs();
    e.winnerIdx = -1;
    e.awaitingItem = Item::Data::RecordOrRequest(e.itemId) == nullptr;

    if (const int old = Find(e.sourceGuid, e.slot); old > 0)
        g_items.erase(g_items.begin() + (old - 1));
    g_items.push_front(e);
    return true;
}

// Sorts the rows the way 0x141A920F0 does. Rank table 0x143BB18B8:
// pass 0, need 2, greed 1.
void SortPlayers(HistItem *e) {
    auto rank = [](int rollType) {
        switch (rollType) {
        case ROLL_NEED:
            return 2;
        case ROLL_GREED:
            return 1;
        default:
            return 0;
        }
    };
    const uint64_t winner = e->winnerGuid;
    std::sort(e->players, e->players + e->playerCount,
              [&](const PlayerRoll &a, const PlayerRoll &b) {
                  const bool aWon = winner != 0 && a.guid == winner;
                  const bool bWon = winner != 0 && b.guid == winner;
                  if (aWon != bWon)
                      return aWon;
                  if (rank(a.rollType) != rank(b.rollType))
                      return rank(a.rollType) > rank(b.rollType);
                  if (a.roll != b.roll)
                      return a.roll > b.roll;
                  return a.order < b.order;
              });
    e->winnerIdx = -1;
    for (int i = 0; winner != 0 && i < e->playerCount; ++i) {
        if (e->players[i].guid == winner) {
            e->winnerIdx = i;
            break;
        }
    }
}

// 0x141A90F30. Returns the 1-based item index and writes the 1-based row
// index, or returns 0 when nothing changed.
int RecordRoll(const void *msg, int *outPlayerIdx) {
    const int itemIdx = Find(Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_SOURCE_GUID),
                             Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_SLOT));
    if (itemIdx == 0)
        return 0;
    HistItem *e = GetEntry(itemIdx);
    const uint64_t roller = Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_ROLLER_GUID);
    int row = -1;
    for (int i = 0; i < e->playerCount; ++i) {
        if (e->players[i].guid == roller) {
            row = i;
            break;
        }
    }
    if (row < 0)
        return 0;

    // WriteRollLine's (FUN_0061C0B0) four cases: a negative byte is a vote,
    // greed for type 2 else pass; a non-negative byte with type 0 is a need
    // vote; anything else is a dice result carrying the number.
    const uint8_t rollByte = Read<uint8_t>(msg, Offsets::OFF_LOOTROLL_ROLL_NUM);
    const int packetType = Read<int32_t>(msg, Offsets::OFF_LOOTROLL_ROLL_TYPE);
    PlayerRoll &p = e->players[row];
    if (static_cast<int8_t>(rollByte) < 0) {
        p.rollType = packetType == 2 ? ROLL_GREED : ROLL_PASS;
        p.roll = 0;
    } else if (packetType == 0) {
        p.rollType = ROLL_NEED;
        p.roll = 0;
    } else {
        p.rollType = DiceRollType(packetType);
        p.roll = rollByte;
    }
    p.voted = true;
    *outPlayerIdx = row + 1;
    return itemIdx;
}

// 0x141A915E0. Returns the 1-based item index, or 0 when nothing changed.
int RecordComplete(const void *msg, uint64_t winner) {
    const int itemIdx = Find(Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_SOURCE_GUID),
                             Read<uint32_t>(msg, Offsets::OFF_LOOTROLL_SLOT));
    if (itemIdx == 0)
        return 0;
    HistItem *e = GetEntry(itemIdx);
    if (e->winnerGuid != 0)
        return 0;
    e->isDone = true;
    e->winnerGuid = winner;
    SortPlayers(e);
    return itemIdx;
}

// ---- Packet co-hooks -------------------------------------------------------
// Signatures match the verified per-handler ABI above (note the differing
// message register, stack arg, and RET). Read the message fields before the
// original runs — it frees the message. Events fire after the original, so
// the engine's own handler stays off our re-entrancy.

using RollFn_t = void(__fastcall *)(void *msg, void *ctx, int a3); // RET 4
using WonFn_t = void(__fastcall *)(void *ctx, void *msg, int a3);  // RET 4
using PassedFn_t = void(__fastcall *)(void *ctx, void *msg);       // RET 0
using StartFn_t = int(__fastcall *)(void *packet);                 // RET 0, returns 1

RollFn_t g_origRoll = nullptr;
WonFn_t g_origWon = nullptr;
PassedFn_t g_origPassed = nullptr;
StartFn_t g_origStart = nullptr;

// The 3.80.2 roll start waits for the item before it adds it; 1.12 adds at
// the packet, so an item that is not cached yet gets its AUTO_SHOW from
// OnItemLoaded.
int __fastcall Start_h(void *packet) {
    const int handled = g_origStart(packet);
    if (Add()) {
        FireFullUpdate();
        Trim();
        const HistItem &e = g_items.front();
        if (!e.awaitingItem)
            FireAutoShowIfBoP(e.rollID, Item::PeekRecord(e.itemId));
    }
    return handled;
}

void __fastcall Roll_h(void *msg, void *ctx, int a3) {
    int itemIdx = 0, playerIdx = 0;
    if (msg != nullptr)
        itemIdx = RecordRoll(msg, &playerIdx);
    g_origRoll(msg, ctx, a3);
    if (itemIdx > 0)
        Event::Custom::Fire(_evRollChanged.Slot(), "%d%d", itemIdx, playerIdx);
}

void __fastcall Won_h(void *ctx, void *msg, int a3) {
    int itemIdx = 0;
    if (msg != nullptr)
        itemIdx = RecordComplete(msg, Read<uint64_t>(msg, Offsets::OFF_LOOTROLL_ROLLER_GUID));
    g_origWon(ctx, msg, a3);
    if (itemIdx > 0)
        Event::Custom::Fire(_evRollComplete.Slot(), "%d", itemIdx);
}

void __fastcall Passed_h(void *ctx, void *msg) {
    int itemIdx = 0;
    if (msg != nullptr)
        itemIdx = RecordComplete(msg, 0);
    g_origPassed(ctx, msg);
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

// The item-load callback the 3.80.2 entry registers (0x141A92870): once the
// item's data arrives, FULL_UPDATE, so a nil link can be read again. Then
// the AUTO_SHOW the roll start could not decide without the item.
void OnItemLoaded(uint32_t itemID, bool success) {
    if (!success)
        return;
    std::vector<uint32_t> rollIDs; // copied: the handlers can change the store
    for (HistItem &e : g_items) {
        if (e.awaitingItem && e.itemId == itemID) {
            e.awaitingItem = false;
            rollIDs.push_back(e.rollID);
        }
    }
    if (rollIDs.empty())
        return;
    FireFullUpdate();
    const uint8_t *record = Item::PeekRecord(itemID);
    for (const uint32_t rollID : rollIDs)
        FireAutoShowIfBoP(rollID, record);
}

const Item::Data::LoadSubscriber _itemLoaded{&OnItemLoaded};

// World teardown empties the store with no event (0x141693A90). The glue
// boot is 1.12's world -> character select point.
void ClearOnLogout() {
    g_items.clear();
}

const Game::GlueBootAutoRegister _glueBoot{&ClearOnLogout};

// ---- Lua surface -----------------------------------------------------------

int __fastcall Script_GetNumItems(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(g_items.size()));
    return 1;
}

// rollID, itemLink, numPlayers, isDone, winnerIdx, isMasterLoot, isCurrency
// (0x141A916D0). Nothing for an index out of range.
int __fastcall Script_GetItem(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetItem(itemIdx)");
        return 0;
    }
    const HistItem *e = GetEntry(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (e == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(e->rollID));
    if (const char *link = Item::Link::FromIDSuffixUnique(e->itemId, e->itemSuffix, e->itemSeed))
        Game::Lua::PushString(L, link);
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushNumber(L, static_cast<double>(e->playerCount));
    Game::Lua::PushBool(L, e->isDone);
    if (e->isDone && e->winnerIdx >= 0)
        Game::Lua::PushNumber(L, static_cast<double>(e->winnerIdx + 1));
    else
        Game::Lua::PushNil(L);
    // 1.12 rolls no master-loot items and has no currencies.
    Game::Lua::PushBool(L, false);
    Game::Lua::PushBool(L, false);
    return 7;
}

// name, class, rollType, roll, isWinner, isMe (0x141A91820). Nothing for an
// index out of range.
int __fastcall Script_GetPlayerInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GetPlayerInfo(itemIdx, playerIdx)");
        return 0;
    }
    const HistItem *e = GetEntry(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (e == nullptr)
        return 0;
    const int p = static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1;
    if (p < 0 || p >= e->playerCount)
        return 0;
    const PlayerRoll &r = e->players[p];

    // Both nil on a name-cache miss; the name gets "-Realm" for a player
    // from another realm.
    const uint8_t *rec = Unit::Identity::PlayerInfoRecord(r.guid);
    const char *name = rec != nullptr ? Game::Ptr<const char>(rec, Offsets::OFF_PLAYER_INFO_NAME)
                                      : nullptr;
    if (name != nullptr && *name != '\0') {
        const std::string full = Player::FullName::Make(
            name, Game::Ptr<const char>(rec, Offsets::OFF_PLAYER_INFO_REALM));
        Game::Lua::PushString(L, full.c_str());
        const char *classToken =
            DBC::ClassToken(Game::Read<uint32_t>(rec, Offsets::OFF_PLAYER_INFO_CLASS));
        if (classToken != nullptr && *classToken != '\0')
            Game::Lua::PushString(L, classToken);
        else
            Game::Lua::PushNil(L);
    } else {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
    }

    // No vote: nil while the roll is open, pass once it is decided.
    if (r.voted)
        Game::Lua::PushNumber(L, static_cast<double>(r.rollType));
    else if (e->isDone)
        Game::Lua::PushNumber(L, static_cast<double>(ROLL_PASS));
    else
        Game::Lua::PushNil(L);
    // The dice result shows only once the roll is decided.
    if (r.voted && r.roll >= 1 && e->isDone)
        Game::Lua::PushNumber(L, static_cast<double>(r.roll));
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushBool(L, p == e->winnerIdx);
    Game::Lua::PushBool(L, r.guid == Unit::Identity::PlayerGuid());
    return 6;
}

// 0x141A91B50: no range check, then an immediate trim.
int __fastcall Script_SetExpiration(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetExpiration(numItemsToSave, secondsToSave)");
        return 0;
    }
    g_numItemsToSave = static_cast<int>(Game::Lua::ToNumber(L, 1));
    g_msToSave = static_cast<int>(Game::Lua::ToNumber(L, 2) * 1000.0);
    Trim();
    return 0;
}

// 0x141A91BE0.
int __fastcall Script_GetExpiration(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(g_numItemsToSave));
    Game::Lua::PushNumber(L, static_cast<double>(g_msToSave) / 1000.0);
    return 2;
}

// A ClassicAPI extension for a "clear history" button.
int __fastcall Script_Clear(void *L) {
    (void)L;
    g_items.clear();
    FireFullUpdate();
    return 0;
}

const Game::Doc::Field kGetNumItemsRets[] = {
    Game::Doc::Req("numItems", "number"),
};
const Game::Doc::Function kGetNumItems{"Returns the number of rolled items in the loot history.",
                                       {}, kGetNumItemsRets};

const Game::Doc::Field kGetItemArgs[] = {
    Game::Doc::Req("itemIdx", "luaIndex", "1 is the newest item."),
};
const Game::Doc::Field kGetItemRets[] = {
    Game::Doc::Opt("rollID", "number", nullptr,
                   "Nil, with the rest, when the index is out of range. The same ID as "
                   "START_LOOT_ROLL gives; unlike the index, it does not change."),
    Game::Doc::Opt("itemLink", "string", nullptr,
                   "Nil while the item data is not loaded; LOOT_HISTORY_FULL_UPDATE fires when "
                   "it arrives."),
    Game::Doc::Opt("numPlayers", "number"),
    Game::Doc::Opt("isDone", "bool", nullptr,
                   "True when the item has a winner or every player passed."),
    Game::Doc::Opt("winnerIdx", "luaIndex", nullptr,
                   "The winner's player index; nil when the roll is open or every player "
                   "passed."),
    Game::Doc::Opt("isMasterLoot", "bool", nullptr, "Always false."),
    Game::Doc::Opt("isCurrency", "bool", nullptr, "Always false."),
};
const Game::Doc::Function kGetItem{"Returns a rolled item in the loot history.", kGetItemArgs,
                                   kGetItemRets};

const Game::Doc::Field kGetPlayerInfoArgs[] = {
    Game::Doc::Req("itemIdx", "luaIndex"),
    Game::Doc::Req("playerIdx", "luaIndex"),
};
const Game::Doc::Field kGetPlayerInfoRets[] = {
    Game::Doc::Opt("name", "string", nullptr,
                   "Nil, with the rest, when either index is out of range; also nil when the "
                   "name is not known. Name-Realm for a player from another realm."),
    Game::Doc::Opt("class", "string", nullptr,
                   "The class token, such as \"WARRIOR\"; nil when the name is not known."),
    Game::Doc::Opt("rollType", "number", nullptr,
                   "0 for pass, 1 for need, 2 for greed. Nil while the player has not voted on "
                   "an open roll; 0 for that player when the roll is decided."),
    Game::Doc::Opt("roll", "number", nullptr,
                   "The dice roll, from 1 to 100; nil while the roll is open and when the player "
                   "has no roll."),
    Game::Doc::Opt("isWinner", "bool"),
    Game::Doc::Opt("isMe", "bool", nullptr, "True when the player is the local player."),
};
const Game::Doc::Function kGetPlayerInfo{
    "Returns a player's vote and roll on a rolled item in the loot history.", kGetPlayerInfoArgs,
    kGetPlayerInfoRets};

const Game::Doc::Field kSetExpirationArgs[] = {
    Game::Doc::Req("numItemsToSave", "number"),
    Game::Doc::Req("secondsToSave", "number"),
};
const Game::Doc::Function kSetExpiration{
    "Sets how long the loot history keeps items: older than secondsToSave, an item is dropped "
    "when more than numItemsToSave items are kept. The defaults are 10 and 600.",
    kSetExpirationArgs, {}};

const Game::Doc::Field kGetExpirationRets[] = {
    Game::Doc::Req("numItemsToSave", "number"),
    Game::Doc::Req("secondsToSave", "number"),
};
const Game::Doc::Function kGetExpiration{"Returns the values that SetExpiration set.", {},
                                         kGetExpirationRets};

const Game::Doc::Function kClear{
    "Clears the loot history and fires LOOT_HISTORY_FULL_UPDATE.", {}, {}, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_LootHistory", "GetNumItems", &Script_GetNumItems,
                                     &kGetNumItems);
    Game::Lua::RegisterTableFunction("C_LootHistory", "GetItem", &Script_GetItem, &kGetItem);
    Game::Lua::RegisterTableFunction("C_LootHistory", "GetPlayerInfo", &Script_GetPlayerInfo,
                                     &kGetPlayerInfo);
    Game::Lua::RegisterTableFunction("C_LootHistory", "SetExpiration", &Script_SetExpiration,
                                     &kSetExpiration);
    Game::Lua::RegisterTableFunction("C_LootHistory", "GetExpiration", &Script_GetExpiration,
                                     &kGetExpiration);
    Game::Lua::RegisterTableFunction("C_LootHistory", "Clear", &Script_Clear, &kClear);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Loot::History
