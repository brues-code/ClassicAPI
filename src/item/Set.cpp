// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `C_Item.GetItemSetID(itemLocation)` /
// `C_Item.GetItemSetIDByID(itemInfo)` /
// `C_Item.GetItemSetInfo(setID)` — modern set-membership and set-
// info accessors. The first two surface the 16th return of modern
// retail's `GetItemInfo` (the item's setID, or nil for non-set items);
// the third reads `ItemSet.dbc` to describe the set itself.
//
// Vanilla 1.12 ships `GetItemSetInfo(setID)` but exposes no way to
// ask "what set does this item belong to" without scanning every
// set's `ItemID[17]` array. The `ItemStats_C` cache has it
// (`m_itemSet` at `+0x1C0`); we read it directly.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/Arg.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>

namespace Item::Set {

namespace {

int PushSetIDForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const uint32_t setID = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_ITEM_SET);
    if (setID == 0)
        return 0; // item isn't part of a set
    Game::Lua::PushNumber(L, static_cast<double>(setID));
    return 1;
}

// Reads the locale-clamped name from an ItemSet.dbc record. ItemSet
// has only 8 locale slots (vs Spell.dbc's 9), so clamp the global
// locale index before indexing.
const char *ReadSetName(const uint8_t *record) {
    int locale = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    if (locale < 0 || locale >= Offsets::ITEMSET_LOCALE_COUNT)
        locale = 0;
    auto *names = reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMSET_NAMES);
    return names[locale];
}

} // namespace

// `C_Item.GetItemSetID(itemLocation)` — returns the `ItemSet.dbc` ID
// the item at `itemLocation` belongs to, or nil if it's not part of
// a set, the slot is empty, or the item isn't in the engine's cache.
static int __fastcall Script_C_Item_GetItemSetID(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemSetID(itemLocation)");
        return 0;
    }
    return PushSetIDForItemID(L, Item::ID::FromCGItem(Item::Location::Resolve(L, 1)));
}

// `C_Item.GetItemSetIDByID(itemInfo)` — same as above but takes an
// itemID / `"item:NNN..."` link / numeric-string directly. Returns nil
// for uncached items and warms the engine's item cache so a follow-up
// call after `GET_ITEM_INFO_RECEIVED` resolves.
static int __fastcall Script_C_Item_GetItemSetIDByID(void *L) {
    return PushSetIDForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

// `C_Item.GetItemSetInfo(setID)` — returns a table describing the
// ItemSet.dbc row, or nil if `setID` doesn't resolve.
//
//   setID              number, echo of input
//   name               string, localized set name
//   requiredSkill      number, skill line ID (0 if none)
//   requiredSkillRank  number (0 if none)
//   items              array of itemIDs in the set (empty slots filtered)
//   bonuses            array of `{ spellID = N, threshold = M }`
//                      tables, one per non-empty bonus slot
static int __fastcall Script_C_Item_GetItemSetInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int setID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (setID <= 0)
        return 0;
    const uint8_t *record = DBC::Record(Offsets::VAR_ITEMSET_RECORDS,
                                         Offsets::VAR_ITEMSET_COUNT,
                                         static_cast<uint32_t>(setID));
    if (record == nullptr)
        return 0;

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "setID", static_cast<double>(setID));

    const char *name = ReadSetName(record);
    Game::Lua::SetFieldString(L, "name", (name != nullptr) ? name : "");

    Game::Lua::SetFieldNumber(L, "requiredSkill", static_cast<double>(
        *reinterpret_cast<const uint32_t *>(
            record + Offsets::OFF_ITEMSET_REQUIRED_SKILL)));
    Game::Lua::SetFieldNumber(L, "requiredSkillRank", static_cast<double>(
        *reinterpret_cast<const uint32_t *>(
            record + Offsets::OFF_ITEMSET_REQUIRED_SKILL_RANK)));

    // items: array of non-zero itemIDs (skip empty slots so callers
    // can iterate via `ipairs` without holes).
    Game::Lua::PushString(L, "items");
    Game::Lua::NewTable(L);
    auto *items = reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSET_ITEM_IDS);
    int nextItemKey = 1;
    for (int i = 0; i < Offsets::ITEMSET_MAX_ITEMS; ++i) {
        if (items[i] == 0)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(nextItemKey++));
        Game::Lua::PushNumber(L, static_cast<double>(items[i]));
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetTable(L, -3); // outer["items"] = innerTable

    // bonuses: array of {spellID, threshold} pairs.
    Game::Lua::PushString(L, "bonuses");
    Game::Lua::NewTable(L);
    auto *spells = reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSET_SPELL_IDS);
    auto *thresholds = reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSET_THRESHOLDS);
    int nextBonusKey = 1;
    for (int i = 0; i < Offsets::ITEMSET_MAX_BONUSES; ++i) {
        if (spells[i] == 0)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(nextBonusKey++));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "spellID", static_cast<double>(spells[i]));
        Game::Lua::SetFieldNumber(L, "threshold", static_cast<double>(thresholds[i]));
        Game::Lua::SetTable(L, -3); // bonuses[N] = {spellID, threshold}
    }
    Game::Lua::SetTable(L, -3); // outer["bonuses"] = innerTable

    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kLocationArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kByIDArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kLocationRets[] = {
    Game::Doc::Opt("setID", "number", nullptr,
                   "Nil when the item is in no set, the slot is empty, or the item is not "
                   "cached."),
};
static const Game::Doc::Field kByIDRets[] = {
    Game::Doc::Opt("setID", "number", nullptr,
                   "Nil when the item is in no set or is not cached."),
};
static const Game::Doc::Function kGetItemSetID{
    "Returns the item set that the item at a location belongs to.", kLocationArgs,
    kLocationRets, nullptr, true};
static const Game::Doc::Function kGetItemSetIDByID{
    "Returns the item set that an item belongs to, and asks the server for an item that is "
    "not cached.",
    kByIDArgs, kByIDRets, nullptr, true};

static const Game::Doc::Field kItemSetBonusFields[] = {
    Game::Doc::Req("spellID", "number", "The spell that the bonus gives."),
    Game::Doc::Req("threshold", "number", "How many pieces of the set must be equipped."),
};
static const Game::Doc::Structure kItemSetBonus{"ItemSetBonus", "Item", kItemSetBonusFields,
                                                "One bonus of an item set."};

static const Game::Doc::Field kItemSetInfoFields[] = {
    Game::Doc::Req("setID", "number"),
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("requiredSkill", "number", "The skill line the set needs; 0 for none."),
    Game::Doc::Req("requiredSkillRank", "number", "0 when the set needs no skill."),
    Game::Doc::Req("items", "table", "An array of the item IDs in the set."),
    Game::Doc::Req("bonuses", "table", "An array of ItemSetBonus."),
};
static const Game::Doc::Structure kItemSetInfo{"ItemSetInfo", "Item", kItemSetInfoFields};

static const Game::Doc::Field kGetItemSetInfoArgs[] = {
    Game::Doc::Req("setID", "number"),
};
static const Game::Doc::Field kGetItemSetInfoRets[] = {
    Game::Doc::Opt("info", "ItemSetInfo", nullptr, "Nil when no item set has the ID."),
};
static const Game::Doc::Function kGetItemSetInfo{
    "Returns the name, items and bonuses of an item set.", kGetItemSetInfoArgs,
    kGetItemSetInfoRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSetID",
                                     &Script_C_Item_GetItemSetID, &kGetItemSetID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSetIDByID",
                                     &Script_C_Item_GetItemSetIDByID, &kGetItemSetIDByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSetInfo",
                                     &Script_C_Item_GetItemSetInfo, &kGetItemSetInfo);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Set
