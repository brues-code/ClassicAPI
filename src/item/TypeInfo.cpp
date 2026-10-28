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

#include "item/TypeInfo.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>
#include <initializer_list>

namespace Item::TypeInfo {

namespace {

int LocaleIndex() {
    return *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
}

} // namespace

// Localized ItemClass.dbc name for a class ID (records indexed by ID, the
// standard DBC shape — same lookup `Script_GetItemInfo` uses). Returns ""
// for an unknown class. Obsolete slots keep the client's literal string
// (classID 3 → "Jewelry(OBSOLETE)", 8 → "Generic(OBSOLETE)", …) — that's
// the real vanilla data; Enum.ItemClass supplies the modern key names.
const char *ClassName(uint32_t classID) {
    const char *s = DBC::LocalizedField(Offsets::VAR_ITEMCLASS_RECORDS,
                                        Offsets::VAR_ITEMCLASS_COUNT, classID,
                                        Offsets::OFF_ITEMCLASS_NAMES);
    return (s != nullptr && s[0] != '\0') ? s : "";
}

// Linear-scan ItemSubClass.dbc for the (classID, subClassID) compound-key
// row — same scan Item::GetData uses. Returns the record or nullptr.
const uint8_t *FindSubClass(uint32_t classID, uint32_t subClassID) {
    const int count = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_ITEMSUBCLASS_COUNT));
    if (count <= 0)
        return nullptr;
    auto *records = *reinterpret_cast<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_ITEMSUBCLASS_RECORDS));
    if (records == nullptr)
        return nullptr;
    for (int i = 0; i < count; ++i) {
        const uint8_t *rec =
            records + i * Offsets::OFF_ITEMSUBCLASS_RECORD_STRIDE;
        if (*reinterpret_cast<const uint32_t *>(
                rec + Offsets::OFF_ITEMSUBCLASS_CLASS_ID) == classID &&
            *reinterpret_cast<const uint32_t *>(
                rec + Offsets::OFF_ITEMSUBCLASS_SUBCLASS_ID) == subClassID)
            return rec;
    }
    return nullptr;
}

// Verbose name first, short name on miss (same fallback the engine and
// GetItemInfoInstant use); locale-applied.
const char *SubClassName(const uint8_t *rec) {
    const int locale = LocaleIndex();
    const char *verbose = *reinterpret_cast<const char *const *>(
        rec + Offsets::OFF_ITEMSUBCLASS_DISPLAY_NAME + locale * 4);
    if (verbose != nullptr && verbose[0] != '\0')
        return verbose;
    const char *shortName = *reinterpret_cast<const char *const *>(
        rec + Offsets::OFF_ITEMSUBCLASS_NAME + locale * 4);
    return (shortName != nullptr && shortName[0] != '\0') ? shortName : "";
}

// Either localized name of the subclass row equals `name`, ignoring case —
// 4.3.4's `FindItemClassTypeCallback` (0x008DAAC0) tests both the verbose
// and the display name, so "One-Handed Swords" and "Sword" both match.
bool SubClassNamed(const uint8_t *rec, const char *name) {
    const int locale = LocaleIndex();
    for (const uint32_t off :
         {Offsets::OFF_ITEMSUBCLASS_DISPLAY_NAME, Offsets::OFF_ITEMSUBCLASS_NAME}) {
        const char *s = *reinterpret_cast<const char *const *>(rec + off + locale * 4);
        if (s != nullptr && s[0] != '\0' && Game::StrCmpI(s, name) == 0)
            return true;
    }
    return false;
}

// The engine's INVTYPE table holds the equipLoc KEY string ("INVTYPE_HEAD",
// …) indexed by inventory type. Returns "" for out-of-range / empty slots.
const char *InvTypeKey(int invType) {
    if (invType < 0 || invType > Offsets::INVTYPE_TABLE_MAX_INDEX)
        return "";
    auto **table = reinterpret_cast<const char **>(
        static_cast<uintptr_t>(Offsets::VAR_INVTYPE_STRING_TABLE));
    const char *s = table[invType];
    return (s != nullptr && s[0] != '\0') ? s : "";
}

namespace {

// `GetItemClassInfo(classID)` → `className` (localized). `nil` for an
// unknown class. Backport of the modern global (also exposed as
// `C_Item.GetItemClassInfo`).
int __fastcall Script_GetItemClassInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetItemClassInfo(classID)");
        return 0;
    }
    const char *name = ClassName(static_cast<uint32_t>(Game::Lua::ToNumber(L, 1)));
    if (name[0] == '\0') {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, name);
    return 1;
}

// `C_Item.GetItemSubClassInfo(classID, subClassID)` →
// `subClassName, subClassUsesInvType`. Also exposed as the global
// `GetItemSubClassInfo(classID, subClassID)` → `name, isArmorType`, the
// same two values (the second is true for the armor material subclasses).
//
// `subClassName` is the localized ItemSubClass.dbc name (verbose form, e.g.
// "One-Handed Swords"; falls back to the short form for subclasses that
// only populate it, e.g. "Consumable"). `subClassUsesInvType` is true for
// subclasses whose items display by inventory slot rather than by subclass
// name — the armor material types (Misc / Cloth / Leather / Mail / Plate),
// per the ItemSubClass flags bit at +0x10. Returns `nil` if the
// (classID, subClassID) pair has no row.
int __fastcall Script_GetItemSubClassInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(
            L, "Usage: C_Item.GetItemSubClassInfo(classID, subClassID)");
        return 0;
    }
    const auto classID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    const auto subClassID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    const uint8_t *rec = FindSubClass(classID, subClassID);
    if (rec == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint32_t flags = *reinterpret_cast<const uint32_t *>(
        rec + Offsets::OFF_ITEMSUBCLASS_FLAGS);
    Game::Lua::PushString(L, SubClassName(rec));
    Game::Lua::PushBool(L,
                        (flags & Offsets::ITEMSUBCLASS_FLAG_USES_INVTYPE) != 0);
    return 2;
}

// `C_Item.GetItemInventorySlotKey(inventorySlot)` → the equipLoc key string
// for an `Enum.InventoryType` value (e.g. `1` → "INVTYPE_HEAD"). `nil` for
// non-equip (0) or out-of-range values.
int __fastcall Script_GetItemInventorySlotKey(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(
            L, "Usage: C_Item.GetItemInventorySlotKey(inventorySlot)");
        return 0;
    }
    const char *key = InvTypeKey(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (key[0] == '\0') {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, key);
    return 1;
}

// `C_Item.GetItemInventorySlotInfo(inventorySlot)` → the localized display
// name for an `Enum.InventoryType` value (e.g. `1` → "Head"). The engine's
// INVTYPE table stores the KEY ("INVTYPE_HEAD"); the display name is the
// FrameXML global string of that name (`INVTYPE_HEAD = "Head"`), so we
// resolve it via that global with the key itself as the fallback. `nil` for
// non-equip (0) or out-of-range values.
int __fastcall Script_GetItemInventorySlotInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(
            L, "Usage: C_Item.GetItemInventorySlotInfo(inventorySlot)");
        return 0;
    }
    const char *key = InvTypeKey(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (key[0] == '\0') {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushLocalizedString(L, key, key);
    return 1;
}

// `Enum.InventoryType` — the modern equip-type enum. Values 0..28 are the
// ones vanilla items actually report; 29..34 (profession / equippable-spell
// types) are post-vanilla but included so backport code that references those
// keys resolves (the comparisons just never match on 1.12 data).
constexpr Game::Lua::EnumIntegerEntry kInventoryTypeEntries[] = {
    {"IndexNonEquipType", 0},
    {"IndexHeadType", 1},
    {"IndexNeckType", 2},
    {"IndexShoulderType", 3},
    {"IndexBodyType", 4},
    {"IndexChestType", 5},
    {"IndexWaistType", 6},
    {"IndexLegsType", 7},
    {"IndexFeetType", 8},
    {"IndexWristType", 9},
    {"IndexHandType", 10},
    {"IndexFingerType", 11},
    {"IndexTrinketType", 12},
    {"IndexWeaponType", 13},
    {"IndexShieldType", 14},
    {"IndexRangedType", 15},
    {"IndexCloakType", 16},
    {"Index2HweaponType", 17},
    {"IndexBagType", 18},
    {"IndexTabardType", 19},
    {"IndexRobeType", 20},
    {"IndexWeaponmainhandType", 21},
    {"IndexWeaponoffhandType", 22},
    {"IndexHoldableType", 23},
    {"IndexAmmoType", 24},
    {"IndexThrownType", 25},
    {"IndexRangedrightType", 26},
    {"IndexQuiverType", 27},
    {"IndexRelicType", 28},
    {"IndexProfessionToolType", 29},
    {"IndexProfessionGearType", 30},
    {"IndexEquipablespellOffensiveType", 31},
    {"IndexEquipablespellUtilityType", 32},
    {"IndexEquipablespellDefensiveType", 33},
    {"IndexEquipablespellWeaponType", 34},
};

// `Enum.ItemClass` — the modern item-class enum. Values 0..15 are the
// classes vanilla items actually report (the numeric IDs match retail; the
// obsolete slots 3/8/10/14 keep their modern key names — Gem /
// ItemEnhancement / CurrencyTokenObsolete / PermanentObsolete — even
// though ItemClass.dbc labels them "…(OBSOLETE)"). 16..19 are post-vanilla
// and never appear in 1.12 item data, included so backport code that
// references those keys resolves (comparisons just never match).
constexpr Game::Lua::EnumIntegerEntry kItemClassEntries[] = {
    {"Consumable", 0},
    {"Container", 1},
    {"Weapon", 2},
    {"Gem", 3},
    {"Armor", 4},
    {"Reagent", 5},
    {"Projectile", 6},
    {"Tradegoods", 7},
    {"ItemEnhancement", 8},
    {"Recipe", 9},
    {"CurrencyTokenObsolete", 10},
    {"Quiver", 11},
    {"Questitem", 12},
    {"Key", 13},
    {"PermanentObsolete", 14},
    {"Miscellaneous", 15},
    {"Glyph", 16},
    {"Battlepet", 17},
    {"WoWToken", 18},
    {"Profession", 19},
};

// `Enum.ItemQuality` — the numeric `quality` reported by `GetItemInfo` /
// `C_Item.GetItemQuality`. Values 0..6 are all vanilla ever uses; the
// post-vanilla Heirloom (7) and WoWToken (8) tiers are omitted (no such
// items exist on 1.12).
constexpr Game::Lua::EnumIntegerEntry kItemQualityEntries[] = {
    {"Poor", 0},
    {"Common", 1},
    {"Uncommon", 2},
    {"Rare", 3},
    {"Epic", 4},
    {"Legendary", 5},
    {"Artifact", 6},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetItemClassInfo",
                                      &Script_GetItemClassInfo);
    Game::Lua::RegisterGlobalFunction("GetItemSubClassInfo",
                                      &Script_GetItemSubClassInfo);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemClassInfo",
                                     &Script_GetItemClassInfo);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSubClassInfo",
                                     &Script_GetItemSubClassInfo);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInventorySlotKey",
                                     &Script_GetItemInventorySlotKey);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInventorySlotInfo",
                                     &Script_GetItemInventorySlotInfo);
    Game::Lua::RegisterIntegerEnum(
        "Enum", "InventoryType", kInventoryTypeEntries,
        sizeof(kInventoryTypeEntries) / sizeof(kInventoryTypeEntries[0]));
    Game::Lua::RegisterIntegerEnum(
        "Enum", "ItemClass", kItemClassEntries,
        sizeof(kItemClassEntries) / sizeof(kItemClassEntries[0]));
    Game::Lua::RegisterIntegerEnum(
        "Enum", "ItemQuality", kItemQualityEntries,
        sizeof(kItemQualityEntries) / sizeof(kItemQualityEntries[0]));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::TypeInfo
