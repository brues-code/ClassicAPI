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

// `C_Item.GetItemDataByID(itemInfo)` / `C_Item.GetItemData(itemLocation)`
// — return a single table containing every field we can extract from
// the cached `ItemStats_C` record. Convenience for addons that want
// the kitchen sink without making 15 separate `C_Item.*` calls.
//
// Returns `nil` if the item isn't cached (caller should
// `RequestLoadItemData` and listen for `ITEM_DATA_LOAD_RESULT`).
// Does not warm the cache itself — this is a passive reader, same as
// `GetItemInfoInstant`'s contract.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/Arg.h"
#include "item/ID.h"
#include "item/Icon.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/Spell.h"

#include <cstdint>

namespace Item::GetData {

namespace {

int CurrentLocaleIndex() {
    return Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
}

// Mirrors the lookup in `Item::Info`. ItemClass.dbc record `+0x0C` is
// the 9-locale name array; null/empty slots fall back to "".
const char *LookupClassName(uint32_t classID) {
    const char *s = DBC::LocalizedField(
        Offsets::VAR_ITEMCLASS_RECORDS, Offsets::VAR_ITEMCLASS_COUNT,
        classID, Offsets::OFF_ITEMCLASS_NAMES);
    return (s != nullptr && s[0] != '\0') ? s : "";
}

// ItemSubClass.dbc walker — same fallback chain as Script_GetItemInfo
// (verbose name first, short name on miss).
const char *LookupSubClassName(uint32_t classID, uint32_t subClassID) {
    const int count = Game::Read<int>(Offsets::VAR_ITEMSUBCLASS_COUNT);
    if (count <= 0)
        return "";
    auto *records = Game::Read<const uint8_t *>(Offsets::VAR_ITEMSUBCLASS_RECORDS);
    if (records == nullptr)
        return "";
    const int locale = CurrentLocaleIndex();
    for (int i = 0; i < count; ++i) {
        const uint8_t *record = records + i * Offsets::OFF_ITEMSUBCLASS_RECORD_STRIDE;
        if (*reinterpret_cast<const uint32_t *>(record + 0x00) != classID)
            continue;
        if (*reinterpret_cast<const uint32_t *>(record + 0x04) != subClassID)
            continue;
        const char *verbose = Game::Read<const char *>(
            record, Offsets::OFF_ITEMSUBCLASS_DISPLAY_NAME + locale * 4);
        if (verbose != nullptr && verbose[0] != '\0')
            return verbose;
        const char *shortName = Game::Read<const char *>(
            record, Offsets::OFF_ITEMSUBCLASS_NAME + locale * 4);
        return (shortName != nullptr && shortName[0] != '\0') ? shortName : "";
    }
    return "";
}

const char *LookupInvType(uint32_t invType) {
    if (invType > Offsets::INVTYPE_TABLE_MAX_INDEX)
        return "";
    auto **table = reinterpret_cast<const char **>(Offsets::VAR_INVTYPE_STRING_TABLE);
    const char *s = table[invType];
    return (s != nullptr && s[0] != '\0') ? s : "";
}

// Wrath flipped the bagFamily encoding from raw 1-based IDs to a
// bitmask. We surface the modern shape so backported addons compile
// cleanly. See `Item::Info::BagFamilyIdToBitmask` for the full story.
uint32_t BagFamilyIdToBitmask(uint32_t rawId) {
    return (rawId == 0) ? 0 : (1u << (rawId - 1));
}

// Shorthand: read a u32 field from the record at `offset`, push it on
// the stack as a Lua number, and set `t[key] = value` on the table at
// stack[-3]. `SetFieldNumber` already handles the push/set; this is
// just the field-fetch convenience.
void SetU32(void *L, const uint8_t *record, const char *key, int offset) {
    const uint32_t v = *reinterpret_cast<const uint32_t *>(record + offset);
    Game::Lua::SetFieldNumber(L, key, static_cast<double>(v));
}

void SetI32(void *L, const uint8_t *record, const char *key, int offset) {
    const int32_t v = *reinterpret_cast<const int32_t *>(record + offset);
    Game::Lua::SetFieldNumber(L, key, static_cast<double>(v));
}

void SetF32(void *L, const uint8_t *record, const char *key, int offset) {
    const float v = *reinterpret_cast<const float *>(record + offset);
    Game::Lua::SetFieldNumber(L, key, static_cast<double>(v));
}

// Sets `t[key]` to a sub-array of N consecutive u32 values from the
// record. Pushes 1-indexed (Lua array convention).
void SetU32Array(void *L, const uint8_t *record, const char *key,
                 int offset, int count) {
    Game::Lua::PushString(L, key);
    Game::Lua::NewTable(L);
    auto *arr = reinterpret_cast<const uint32_t *>(record + offset);
    for (int i = 0; i < count; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::PushNumber(L, static_cast<double>(arr[i]));
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetTable(L, -3);
}

void SetI32Array(void *L, const uint8_t *record, const char *key,
                 int offset, int count) {
    Game::Lua::PushString(L, key);
    Game::Lua::NewTable(L);
    auto *arr = reinterpret_cast<const int32_t *>(record + offset);
    for (int i = 0; i < count; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::PushNumber(L, static_cast<double>(arr[i]));
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetTable(L, -3);
}

void SetF32Array(void *L, const uint8_t *record, const char *key,
                 int offset, int count) {
    Game::Lua::PushString(L, key);
    Game::Lua::NewTable(L);
    auto *arr = reinterpret_cast<const float *>(record + offset);
    for (int i = 0; i < count; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::PushNumber(L, static_cast<double>(arr[i]));
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetTable(L, -3);
}

// Sparse stats: walks the 10 (statType, statValue) slots and pushes
// only the populated ones into a map keyed by statType. Modern addons
// expect `stats[STAT_AGI] = 5`-style access; matching that here avoids
// callers having to walk the fixed-10 array themselves.
void SetStatsMap(void *L, const uint8_t *record) {
    Game::Lua::PushString(L, "stats");
    Game::Lua::NewTable(L);
    auto *types = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_STAT_TYPE);
    auto *values = Game::Ptr<const int32_t>(
        record, Offsets::OFF_ITEMSTATS_STAT_VALUE);
    for (int i = 0; i < Offsets::ITEMSTATS_STAT_SLOT_COUNT; ++i) {
        if (types[i] == 0 && values[i] == 0)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(types[i]));
        Game::Lua::PushNumber(L, static_cast<double>(values[i]));
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetTable(L, -3);
}

// Spell slots: walks all 5 slots, pushes only entries with a non-zero
// spellID as `{ id, trigger, charges, cooldown, category,
// categoryCooldown }` records. Sparse but 1-indexed in declaration
// order so the slot index is preserved.
void SetSpellsArray(void *L, const uint8_t *record) {
    Game::Lua::PushString(L, "spells");
    Game::Lua::NewTable(L);
    auto *ids = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_ID);
    auto *triggers = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER);
    auto *charges = Game::Ptr<const int32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_CHARGES);
    auto *cooldowns = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_COOLDOWN);
    auto *categories = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_CATEGORY);
    auto *catCooldowns = Game::Ptr<const uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SPELL_CATEGORY_CD);
    int written = 0;
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
        if (ids[i] == 0)
            continue;
        ++written;
        Game::Lua::PushNumber(L, static_cast<double>(written));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "id", static_cast<double>(ids[i]));
        Game::Lua::SetFieldNumber(L, "trigger", static_cast<double>(triggers[i]));
        Game::Lua::SetFieldNumber(L, "charges", static_cast<double>(charges[i]));
        Game::Lua::SetFieldNumber(L, "cooldown", static_cast<double>(cooldowns[i]));
        Game::Lua::SetFieldNumber(L, "category", static_cast<double>(categories[i]));
        Game::Lua::SetFieldNumber(L, "categoryCooldown",
                                  static_cast<double>(catCooldowns[i]));
        Game::Lua::SetTable(L, -3);
    }
    Game::Lua::SetTable(L, -3);
}

// Pushes a single Lua table for `record` onto the stack, populated
// with every field we can read out of `ItemStats_C`. Caller is
// responsible for verifying `record` is non-null and pushing the
// itemID as a setup step.
void PushItemDataTable(void *L, uint32_t itemID, const uint8_t *record) {
    Game::Lua::NewTable(L);

    Game::Lua::SetFieldNumber(L, "itemID", static_cast<double>(itemID));

    // Identity strings
    const char *name = Game::Read<const char *>(
        record, Offsets::OFF_ITEMSTATS_NAME);
    Game::Lua::SetFieldString(L, "name", name);
    const char *description = Game::Read<const char *>(
        record, Offsets::OFF_ITEMSTATS_DESCRIPTION);
    if (description != nullptr && description[0] != '\0')
        Game::Lua::SetFieldString(L, "description", description);

    // Display
    const uint32_t displayInfoID = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    Game::Lua::SetFieldNumber(L, "displayInfoID", static_cast<double>(displayInfoID));
    char iconPath[260];
    if (Item::Icon::PathForDisplayInfoID(displayInfoID, iconPath, sizeof(iconPath)))
        Game::Lua::SetFieldString(L, "icon", iconPath);

    // Quality / classification
    const uint32_t quality = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_QUALITY);
    Game::Lua::SetFieldNumber(L, "quality", static_cast<double>(quality));

    const uint32_t classID = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t subClassID = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_SUBCLASS);
    Game::Lua::SetFieldNumber(L, "classID", static_cast<double>(classID));
    Game::Lua::SetFieldNumber(L, "subclassID", static_cast<double>(subClassID));
    Game::Lua::SetFieldString(L, "className", LookupClassName(classID));
    Game::Lua::SetFieldString(L, "subclassName", LookupSubClassName(classID, subClassID));

    const uint32_t invType = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    Game::Lua::SetFieldNumber(L, "inventoryType", static_cast<double>(invType));
    Game::Lua::SetFieldString(L, "equipLoc", LookupInvType(invType));

    // Bind
    SetU32(L, record, "bindType", Offsets::OFF_ITEMSTATS_BONDING);

    // Flags: raw + decoded bits.
    const uint32_t flags = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_FLAGS);
    Game::Lua::SetFieldNumber(L, "flags", static_cast<double>(flags));
    Game::Lua::SetFieldBool(L, "isConjured",
                            (flags & Offsets::ITEM_FLAG_CONJURED) != 0);
    Game::Lua::SetFieldBool(L, "isOpenable",
                            (flags & Offsets::ITEMSTATS_FLAG_OPENABLE) != 0);
    Game::Lua::SetFieldBool(L, "isLootable",
                            (flags & Offsets::ITEM_FLAG_LOOTABLE) != 0);
    Game::Lua::SetFieldBool(L, "isWrapper",
                            (flags & Offsets::ITEM_FLAG_WRAPPER) != 0);

    // Stacking / inventory
    SetI32(L, record, "maxStackSize", Offsets::OFF_ITEMSTATS_STACKABLE);
    SetI32(L, record, "maxCount", Offsets::OFF_ITEMSTATS_MAX_COUNT);
    SetU32(L, record, "containerSlots", Offsets::OFF_ITEMSTATS_CONTAINER_SLOTS);
    const uint32_t rawBagFamily = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_BAG_FAMILY);
    Game::Lua::SetFieldNumber(L, "bagFamily",
                              static_cast<double>(BagFamilyIdToBitmask(rawBagFamily)));

    // Economy
    SetU32(L, record, "buyPrice", Offsets::OFF_ITEMSTATS_BUY_PRICE);
    SetU32(L, record, "sellPrice", Offsets::OFF_ITEMSTATS_SELL_PRICE);

    // Level / requirements
    SetU32(L, record, "itemLevel", Offsets::OFF_ITEMSTATS_ITEM_LEVEL);
    SetU32(L, record, "requiredLevel", Offsets::OFF_ITEMSTATS_REQUIRED_LEVEL);
    SetU32(L, record, "requiredSkill", Offsets::OFF_ITEMSTATS_REQUIRED_SKILL);
    SetU32(L, record, "requiredSkillRank", Offsets::OFF_ITEMSTATS_REQUIRED_SKILL_RANK);
    SetU32(L, record, "requiredSpell", Offsets::OFF_ITEMSTATS_REQUIRED_SPELL);
    SetU32(L, record, "requiredHonorRank", Offsets::OFF_ITEMSTATS_REQUIRED_HONOR_RANK);
    SetU32(L, record, "requiredCityRank", Offsets::OFF_ITEMSTATS_REQUIRED_CITY_RANK);
    SetU32(L, record, "requiredFaction", Offsets::OFF_ITEMSTATS_REQUIRED_FACTION);
    SetU32(L, record, "requiredFactionRank", Offsets::OFF_ITEMSTATS_REQUIRED_FACTION_RANK);
    SetU32(L, record, "allowableClass", Offsets::OFF_ITEMSTATS_ALLOWABLE_CLASS);
    SetU32(L, record, "allowableRace", Offsets::OFF_ITEMSTATS_ALLOWABLE_RACE);

    // Equipment defenses
    SetU32(L, record, "armor", Offsets::OFF_ITEMSTATS_ARMOR);
    SetU32(L, record, "block", Offsets::OFF_ITEMSTATS_BLOCK);
    SetU32(L, record, "maxDurability", Offsets::OFF_ITEMSTATS_MAX_DURABILITY);

    // Sparse stats map (statType -> statValue, only populated entries)
    SetStatsMap(L, record);

    // Resistances
    SetU32(L, record, "resistanceHoly", Offsets::OFF_ITEMSTATS_RES_HOLY);
    SetU32(L, record, "resistanceFire", Offsets::OFF_ITEMSTATS_RES_FIRE);
    SetU32(L, record, "resistanceNature", Offsets::OFF_ITEMSTATS_RES_NATURE);
    SetU32(L, record, "resistanceFrost", Offsets::OFF_ITEMSTATS_RES_FROST);
    SetU32(L, record, "resistanceShadow", Offsets::OFF_ITEMSTATS_RES_SHADOW);
    SetU32(L, record, "resistanceArcane", Offsets::OFF_ITEMSTATS_RES_ARCANE);

    // Weapon
    SetF32Array(L, record, "damageMin", Offsets::OFF_ITEMSTATS_DAMAGE_MIN,
                Offsets::ITEMSTATS_DAMAGE_SLOT_COUNT);
    SetF32Array(L, record, "damageMax", Offsets::OFF_ITEMSTATS_DAMAGE_MAX,
                Offsets::ITEMSTATS_DAMAGE_SLOT_COUNT);
    SetU32Array(L, record, "damageType", Offsets::OFF_ITEMSTATS_DAMAGE_TYPE,
                Offsets::ITEMSTATS_DAMAGE_SLOT_COUNT);
    SetU32(L, record, "delay", Offsets::OFF_ITEMSTATS_DELAY);
    SetU32(L, record, "ammoType", Offsets::OFF_ITEMSTATS_AMMO_TYPE);
    SetF32(L, record, "rangedModRange", Offsets::OFF_ITEMSTATS_RANGED_MOD_RANGE);

    // Spell slots (sparse array of records) + ON_USE convenience
    SetSpellsArray(L, record);
    const int useSpellID = Item::Spell::OnUseSpellIDForItemID(itemID);
    if (useSpellID > 0)
        Game::Lua::SetFieldNumber(L, "useSpellID", static_cast<double>(useSpellID));

    // Lock / item set
    SetU32(L, record, "lockID", Offsets::OFF_ITEMSTATS_LOCK_ID);
    SetU32(L, record, "itemSet", Offsets::OFF_ITEMSTATS_ITEM_SET);

    // Books / quest / binding
    SetU32(L, record, "pageText", Offsets::OFF_ITEMSTATS_PAGE_TEXT);
    SetU32(L, record, "pageMaterial", Offsets::OFF_ITEMSTATS_PAGE_MATERIAL);
    SetU32(L, record, "languageID", Offsets::OFF_ITEMSTATS_LANGUAGE_ID);
    SetU32(L, record, "startQuest", Offsets::OFF_ITEMSTATS_START_QUEST);
    SetI32(L, record, "material", Offsets::OFF_ITEMSTATS_MATERIAL);
    SetU32(L, record, "sheath", Offsets::OFF_ITEMSTATS_SHEATH);
    SetI32(L, record, "randomProperty", Offsets::OFF_ITEMSTATS_RANDOM_PROPERTY);
    SetU32(L, record, "area", Offsets::OFF_ITEMSTATS_AREA);
    SetU32(L, record, "map", Offsets::OFF_ITEMSTATS_MAP);
}

// Common path for both entry points: take an itemID, peek the cache,
// build & push the data table or push nil for uncached.
int PushDataForItemID(void *L, int itemID) {
    if (itemID <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushItemDataTable(L, static_cast<uint32_t>(itemID), record);
    return 1;
}

// `C_Item.GetItemDataByID(itemInfo)` — accepts numeric itemID,
// `"item:NNN..."` string, or full chat link. Returns the data table
// or nil for invalid input / uncached items.
int __fastcall Script_GetItemDataByID(void *L) {
    return PushDataForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

// `C_Item.GetItemData(itemLocation)` — resolves the equipment-slot /
// bag-slot location to its CGItem, extracts the itemID, then peeks
// the cache. Returns nil for empty slots, invalid locations, or
// uncached items.
int __fastcall Script_GetItemData(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemData(itemLocation)");
        return 0;
    }
    return PushDataForItemID(L, Item::ID::FromCGItem(Item::Location::Resolve(L, 1)));
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemDataByID", &Script_GetItemDataByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemData", &Script_GetItemData);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::GetData
