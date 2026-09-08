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
#include "dbc/Lookup.h"
#include "item/Arg.h"
#include "item/BagFamily.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Item::Info {

static const char *PushedOrEmpty(const char *s) { return (s != nullptr && s[0] != 0) ? s : ""; }

static int CurrentLocaleIndex() { return Game::Read<int>(Offsets::VAR_LOCALE_INDEX); }

static const char *LookupItemClassName(uint32_t classID) {
    return PushedOrEmpty(DBC::LocalizedField(
        Offsets::VAR_ITEMCLASS_RECORDS, Offsets::VAR_ITEMCLASS_COUNT,
        classID, Offsets::OFF_ITEMCLASS_NAMES));
}

static const char *LookupItemSubClassName(uint32_t classID, uint32_t subClassID) {
    const int count = Game::Read<int>(Offsets::VAR_ITEMSUBCLASS_COUNT);
    if (count <= 0)
        return "";
    auto *records = Game::Read<const uint8_t *>(Offsets::VAR_ITEMSUBCLASS_RECORDS);
    if (records == nullptr)
        return "";
    const int locale = CurrentLocaleIndex();
    for (int i = 0; i < count; i++) {
        const uint8_t *record = records + i * Offsets::OFF_ITEMSUBCLASS_RECORD_STRIDE;
        if (*reinterpret_cast<const uint32_t *>(record + 0x00) != classID)
            continue;
        if (*reinterpret_cast<const uint32_t *>(record + 0x04) != subClassID)
            continue;
        // Mirror Script_GetItemInfo's fallback chain at 0x48E311..0x48E32C:
        // try the verbose name first, fall back to the short name.
        const char *verbose = Game::Read<const char *>(
            record, Offsets::OFF_ITEMSUBCLASS_DISPLAY_NAME + locale * 4);
        if (verbose != nullptr && verbose[0] != 0)
            return verbose;
        const char *shortName = Game::Read<const char *>(
            record, Offsets::OFF_ITEMSUBCLASS_NAME + locale * 4);
        return PushedOrEmpty(shortName);
    }
    return "";
}

static const char *LookupInvType(uint32_t invType) {
    if (invType > Offsets::INVTYPE_TABLE_MAX_INDEX)
        return "";
    auto **table = reinterpret_cast<const char **>(Offsets::VAR_INVTYPE_STRING_TABLE);
    return PushedOrEmpty(table[invType]);
}

static bool BuildIconPath(uint32_t displayInfoID, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = 0;
    const char *iconName = DBC::StringField(
        Offsets::VAR_ITEMDISPLAYINFO_RECORDS, Offsets::VAR_ITEMDISPLAYINFO_COUNT,
        displayInfoID, Offsets::OFF_ITEMDISPLAYINFO_ICON);
    if (iconName == nullptr)
        return false;
    snprintf(out, outSize, "Interface\\Icons\\%s", iconName);
    return true;
}

// `GetItemInfoInstant` is the modern "synchronous, never blocks" item
// info call. In modern WoW the trailing 6 fields come from client-side
// `Item-sparse.db2` and so the call always returns the full 7-tuple.
// 1.12 has no client-side item DB — those fields live only in the
// network-fed `DBCache_ItemStats`. We mirror the contract by:
//   - always returning the itemID (echoed from input) so callers using
//     `GetItemInfoInstant(link_or_id)` as an ID extractor never see nil
//   - returning the remaining 6 fields from the cache when present,
//     nil when not (preserving the 7-tuple shape)
//   - not warming the cache; "Instant" implies no side effects. Callers
//     that need the full info should use `GetItemInfo`, which already
//     triggers the warmup via the `Script_GetItemInfo` hook.
static int __fastcall Script_GetItemInfoInstant(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));

    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        for (int i = 0; i < 6; ++i)
            Game::Lua::PushNil(L);
        return 7;
    }

    const uint32_t classID =
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t subClassID =
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SUBCLASS);
    const uint32_t displayInfoID =
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    const uint32_t invType =
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);

    char iconPath[260];
    if (!BuildIconPath(displayInfoID, iconPath, sizeof(iconPath)))
        iconPath[0] = 0;

    Game::Lua::PushString(L, LookupItemClassName(classID));
    Game::Lua::PushString(L, LookupItemSubClassName(classID, subClassID));
    Game::Lua::PushString(L, LookupInvType(invType));
    Game::Lua::PushString(L, iconPath);
    Game::Lua::PushNumber(L, static_cast<double>(classID));
    Game::Lua::PushNumber(L, static_cast<double>(subClassID));
    return 7;
}

// Pushes the icon path for `itemID` and returns 1, or returns 0 (= nil
// to Lua) if the item isn't cached, the displayInfoID is missing, or
// the icon record's path slot is empty. Shared by all three
// icon-accessor entry points.
static int PushIconForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const uint32_t displayInfoID = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    char iconPath[260];
    if (!BuildIconPath(displayInfoID, iconPath, sizeof(iconPath)))
        return 0;
    Game::Lua::PushString(L, iconPath);
    return 1;
}

// `GetItemIcon(itemID)` — global, takes a numeric itemID only. Modern
// WoW returns a fileID:number; we surface the icon path string since
// 1.12 has no fileID system. Direct passthrough to
// `texture:SetTexture(...)`.
static int __fastcall Script_GetItemIcon(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetItemIcon(itemID)");
        return 0;
    }
    return PushIconForItemID(L, static_cast<int>(Game::Lua::ToNumber(L, 1)));
}

// `C_Item.GetItemIcon(itemLocation)` — table-shape ItemLocation form.
// Routes through `Item::Location::Resolve` → CGItem → itemID, then the
// same cache lookup as the other variants.
static int __fastcall Script_C_Item_GetItemIcon(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemIcon(itemLocation)");
        return 0;
    }
    const int itemID = Item::ID::FromCGItem(Item::Location::Resolve(L, 1));
    return PushIconForItemID(L, itemID);
}

// `C_Item.GetItemIconByID(itemInfo)` — accepts numeric itemID or
// `"item:NN"` string (full chat links work too via the same parser
// `GetItemInfoInstant` uses).
static int __fastcall Script_C_Item_GetItemIconByID(void *L) {
    return PushIconForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

// `C_Item.GetItemFamily(item)` — returns the BagFamily bitmask for an
// item (modern encoding: `1 << (ID-1)`), or `nil` if the item isn't
// in the cache. Used by auto-routing addons that decide which
// specialty bag a looted item should land in.
//
// Accepts a numeric `itemID` or a string containing `"item:NNN"` (full
// chat links work) — same parser as `GetItemInfoInstant`.
//
// **Auto-warmup on cache miss.** Returns nil for uncached items but
// kicks off the cache fill in the background, so a follow-up call
// after `GET_ITEM_INFO_RECEIVED` lands the value. Mirrors the
// observed 1.15 behavior (nil first, value after retry) — the engine
// itself triggers the load. We diverge from 1.15 in one detail: we
// fire `GET_ITEM_INFO_RECEIVED` when the data arrives (matching our
// other implicit-warmup paths like `GetItemInfo` and `SetItemByID`),
// whereas 1.15's `GetItemFamily` is a silent fill. Consistency
// within our DLL's event surface > faithful 1.15 mimicry — addons
// that already listen for the event get notified for free.
//
// Modern WoW returns 0 for items the cache lookup fails on; we return
// nil so callers can distinguish "item not cached" from "item exists
// but has family 0 (general-purpose)". Both are safe to treat as "no
// preference" but keeping them distinct helps debugging.
//
// 1.12 vanilla server data leaves the `m_bagFamily` field empty on the
// **bags/quivers** themselves — Soul/Herb/Enchanting/Engineering bags AND
// quivers/ammo pouches all read 0. Individual loot items do carry the
// field reliably (arrows, bullets, shards, herbs return their proper raw
// IDs). For a bag whose field is empty we recover the family from its
// class + subclass (the same signal the "Soul Bag" tooltip line uses), so
// a Felcloth Bag reports the Soul Shard family (0x4), a quiver 0x1, an
// ammo pouch 0x2 — see `Item::BagFamily`.
static int __fastcall Script_C_Item_GetItemFamily(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    // Family field if populated, else derived from the container subclass
    // (Turtle leaves bags' m_bagFamily at 0 — see Item::BagFamily).
    Game::Lua::PushNumber(
        L, static_cast<double>(Item::BagFamily::BitmaskForRecord(record)));
    return 1;
}

// `C_Item.GetItemInfo(itemInfo)` — the full modern info tuple, sourced
// entirely from the cached `ItemStats_C` record (+ the class/subclass/invtype
// DBC name lookups this file already does for `GetItemInfoInstant`). Accepts
// a numeric itemID, an `"item:NNN"` string, or a full chat link — same parser
// as `GetItemInfoInstant`.
//
// Returns (18 values):
//   itemName, itemLink, itemQuality, itemLevel, itemMinLevel, itemType,
//   itemSubType, itemStackCount, itemEquipLoc, itemTexture, sellPrice,
//   classID, subclassID, bindType, expansionID, setID, isCraftingReagent,
//   itemDescription
//
// `expansionID` is always 0 (vanilla) and `isCraftingReagent` always false
// (no such flag in 1.12's item data); `setID` is nil when the item has no
// set. On a cache miss it warms the cache and returns nothing (nil) —
// the value lands on a retry after `GET_ITEM_INFO_RECEIVED`,
// matching the modern async contract.
static int __fastcall Script_C_Item_GetItemInfo(void *L) {
    const Item::Arg::Resolved arg = Item::Arg::Resolve(L, 1);
    const int itemID = arg.itemID;
    if (itemID <= 0)
        return 0;

    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0; // nil this call; GET_ITEM_INFO_RECEIVED fires when ready
    }

    auto u32 = [record](int off) {
        return *reinterpret_cast<const uint32_t *>(record + off);
    };
    const uint32_t quality = u32(Offsets::OFF_ITEMSTATS_QUALITY);
    const uint32_t itemLevel = u32(Offsets::OFF_ITEMSTATS_ITEM_LEVEL);
    const uint32_t reqLevel = u32(Offsets::OFF_ITEMSTATS_REQUIRED_LEVEL);
    const uint32_t classID = u32(Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t subClassID = u32(Offsets::OFF_ITEMSTATS_SUBCLASS);
    const uint32_t stackCount = u32(Offsets::OFF_ITEMSTATS_STACKABLE);
    const uint32_t invType = u32(Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    const uint32_t displayInfoID = u32(Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    const uint32_t sellPrice = u32(Offsets::OFF_ITEMSTATS_SELL_PRICE);
    const uint32_t bindType = u32(Offsets::OFF_ITEMSTATS_BONDING);
    const uint32_t setID = u32(Offsets::OFF_ITEMSTATS_ITEM_SET);
    const char *name = Game::Read<const char *>(
        record, Offsets::OFF_ITEMSTATS_NAME);
    const char *description = Game::Read<const char *>(
        record, Offsets::OFF_ITEMSTATS_DESCRIPTION);

    // Apply the link's random suffix ("... of the Owl") to both the name and
    // the reconstructed link when the input carried one (arg.suffix); with no
    // suffix these reduce to the base name / plain `item:N:0:0:0` link.
    char suffixedName[128];
    const char *displayName =
        Item::Link::NameFromIDSuffix(static_cast<uint32_t>(itemID), arg.suffix,
                                     suffixedName, sizeof(suffixedName))
            ? suffixedName
            : name;

    char iconPath[260];
    if (!BuildIconPath(displayInfoID, iconPath, sizeof(iconPath)))
        iconPath[0] = 0;
    char link[256];
    const bool haveLink = Item::Link::BasicFromIDSuffix(static_cast<uint32_t>(itemID),
                                                        arg.suffix, link, sizeof(link));

    Game::Lua::PushString(L, PushedOrEmpty(displayName));             // 1  itemName
    if (haveLink)
        Game::Lua::PushString(L, link);                              // 2  itemLink
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushNumber(L, static_cast<double>(quality));           // 3  itemQuality
    Game::Lua::PushNumber(L, static_cast<double>(itemLevel));         // 4  itemLevel
    Game::Lua::PushNumber(L, static_cast<double>(reqLevel));          // 5  itemMinLevel
    Game::Lua::PushString(L, LookupItemClassName(classID));           // 6  itemType
    Game::Lua::PushString(L, LookupItemSubClassName(classID, subClassID)); // 7 itemSubType
    Game::Lua::PushNumber(L, static_cast<double>(stackCount));        // 8  itemStackCount
    Game::Lua::PushString(L, LookupInvType(invType));                 // 9  itemEquipLoc
    Game::Lua::PushString(L, iconPath);                               // 10 itemTexture
    Game::Lua::PushNumber(L, static_cast<double>(sellPrice));         // 11 sellPrice
    Game::Lua::PushNumber(L, static_cast<double>(classID));           // 12 classID
    Game::Lua::PushNumber(L, static_cast<double>(subClassID));        // 13 subclassID
    Game::Lua::PushNumber(L, static_cast<double>(bindType));          // 14 bindType
    Game::Lua::PushNumber(L, 0.0);                                    // 15 expansionID (classic)
    if (setID != 0)
        Game::Lua::PushNumber(L, static_cast<double>(setID));        // 16 setID
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushBool(L, false);                                   // 17 isCraftingReagent
    Game::Lua::PushString(L, PushedOrEmpty(description));            // 18 itemDescription
    return 18;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInfo", &Script_C_Item_GetItemInfo);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInfoInstant", &Script_GetItemInfoInstant);
    Game::Lua::RegisterGlobalFunction("GetItemIcon", &Script_GetItemIcon);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemIcon", &Script_C_Item_GetItemIcon);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemIconByID", &Script_C_Item_GetItemIconByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemFamily", &Script_C_Item_GetItemFamily);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Info
