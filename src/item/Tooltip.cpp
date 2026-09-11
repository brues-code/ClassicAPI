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
#include "item/Data.h"
#include "item/ID.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/QualityColor.h"
#include "item/Record.h"
#include "item/TooltipItem.h"
#include "object/Resolve.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Item::Tooltip {

// `GameTooltip:SetItemByID(itemID)` — modern method that renders an
// item tooltip from just an itemID. The 1.12 workaround was to
// construct an item hyperlink and call `SetHyperlink` —
// `tooltip:SetHyperlink("item:" .. id .. ":0:0:0:0:0:0:0")` — which
// works but forces every caller to know the hyperlink format.
//
// Implementation: format the hyperlink string in C and dispatch to the
// existing `Script_GameTooltip_SetHyperlink` (registry slot 12 at
// `0x00531FD0`). Same registration pattern as `SetSpellByID` in
// [src/spell/Tooltip.cpp](src/spell/Tooltip.cpp).
static int __fastcall Script_GameTooltipSetItemByID(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetItemByID(itemID)");
        return 0;
    }
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetItemByID(itemID)");
        return 0;
    }
    const int itemID = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (itemID <= 0)
        return 0;

    // Warm the cache if uncached. The cache-load callback fires
    // `GET_ITEM_INFO_RECEIVED` when data arrives. Addons that want
    // the tooltip to auto-refresh in place should listen for that
    // event and re-call SetItemByID — modern WoW (5.x+) does the
    // same internally; we don't replicate that engine-level hook
    // because the only viable C-side path here is invoking Lua
    // from a network callback, which has its own pitfalls.
    Item::Data::WarmCache(static_cast<uint32_t>(itemID));

    char hyperlink[64];
    std::snprintf(hyperlink, sizeof(hyperlink), "item:%d:0:0:0:0:0:0:0", itemID);

    // Replace stack[2] (the itemID) with the formatted hyperlink, so
    // the existing SetHyperlink sees `(self, "item:...")`.
    Game::Lua::SetTop(L, 1);            // keep self at stack[1]
    Game::Lua::PushString(L, hyperlink); // stack[2] = hyperlink

    using Script_t = int(__fastcall *)(void *L);
    auto fn = reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_HYPERLINK);
    return fn(L);
}

// `GameTooltip:SetInventoryItemByID(itemID)` — modern method that
// renders the tooltip for the **equipped instance** of `itemID`,
// including enchants, random suffix stats, and broken/locked state.
// Distinct from `SetItemByID`, which renders the base ItemSparse
// data (clean, no enchants).
//
// Verified empirically: with run-speed-enchanted boots equipped,
// `SetInventoryItemByID(<bootsID>)` shows the enchant line in
// addition to the base stats; `SetItemByID(<bootsID>)` shows the
// boots without enchants.
//
// Implementation: walk character-pane slots 1..19 looking for an
// equipped item matching `itemID`; on hit, dispatch to the
// engine's existing `Script_GameTooltip_SetInventoryItem`
// (registry slot 19, `0x00532EE0`) with `("player", slot)`. The
// engine's function reads the actual CGItem instance (with
// descriptor flags + applied enchants) for the tooltip.
//
// Silent no-op if the item isn't equipped — caller should fall
// back to `SetItemByID` for unworn items.
static int __fastcall Script_GameTooltipSetInventoryItemByID(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetInventoryItemByID(itemID)");
        return 0;
    }
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetInventoryItemByID(itemID)");
        return 0;
    }
    const int itemID = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (itemID <= 0)
        return 0;

    // Walk in ascending slot order (1..19). When the player has
    // duplicates of the same itemID equipped (matched MH/OH weapons,
    // identical rings, identical trinkets), modern client behavior
    // is to render the lower-numbered slot — MAINHAND (16) before
    // OFFHAND (17), FINGER1 (11) before FINGER2 (12), TRINKET1 (13)
    // before TRINKET2 (14). Verified empirically against the modern
    // client; our ascending walk + first-match-break naturally
    // matches.
    int foundSlot = 0;
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST;
         slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        const uint8_t *item = Item::Location::ResolveEquipmentSlot(slot);
        if (item == nullptr)
            continue;
        if (Item::ID::FromCGItem(item) == itemID) {
            foundSlot = slot;
            break;
        }
    }
    if (foundSlot == 0)
        return 0;

    // Replace the itemID arg with ("player", slot) so the engine's
    // SetInventoryItem dispatcher reads its expected (unit, slot).
    Game::Lua::SetTop(L, 1);  // keep self at stack[1]
    Game::Lua::PushString(L, "player");
    Game::Lua::PushNumber(L, static_cast<double>(foundSlot));

    using Script_t = int(__fastcall *)(void *L);
    auto fn = reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_INVENTORY_ITEM);
    return fn(L);
}


// Resolves a stored item GUID into a CGItem via the engine's own
// resolver. Same path Item::Count uses for direct bank reads — no
// gating, no inventory walk, works for any object the engine has
// loaded (player bags, equipment, merchant items, loot, trade,
// mailbox, etc.).
static void *ResolveItemByGuid(uint32_t guidLo, uint32_t guidHi) {
    if (guidLo == 0 && guidHi == 0)
        return nullptr;
    return Object::ByGuid(Offsets::TYPEMASK_ITEM,
                          (static_cast<uint64_t>(guidHi) << 32) | guidLo,
                          "GameTooltip:GetItem", 0x172);
}

// `GameTooltip:GetItem()` → (name, link, itemID) for whichever item
// the tooltip is currently displaying. The third return is a
// non-modern addition — modern WoW only returns (name, link) and
// expects addons to parse the itemID out of the link. Returning it
// directly saves callers a gsub round.
//
// BuildItemTooltip stores the displayed item on the tooltip per Set*
// call — but only ONE of two fields depending on the path:
//   - tooltip+0x398 → itemID  (link paths with no instance: SetItemByID,
//                              SetHyperlink)
//   - tooltip+0x380/+0x384 → item GUID  (CGItem paths: SetBagItem,
//                              SetInventoryItem, SetLootItem,
//                              SetMerchantItem, …) — these leave the itemID
//                              field 0, so when +0x398 is 0 we recover the
//                              itemID from the GUID's live CGItem.
// Both are zeroed by the per-tooltip Clear at FUN_00530050.
//
// Two paths for the link:
//   - GUID non-zero → resolve to CGItem and dispatch to the engine's
//     own link builder at FUN_0052AE00. This produces the full dressed
//     link with enchant ID, random suffix factor, unique ID, and
//     random-suffix-decorated name. Same output as
//     GetInventoryItemLink / GetContainerItemLink for that item.
//   - GUID zero → SetItemByID-style tooltip; we have no per-instance
//     data, so build a basic colored link from the cached itemID and
//     quality (`|cff..|Hitem:N:0:0:0:0:0:0:0|h[Name]|h|r`).
//
// Returns nothing for: non-item tooltip (itemID == 0), uncached
// itemID on the no-GUID path (fires a background cache warmup), or
// empty name.
static int __fastcall Script_GameTooltipGetItem(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:GetItem()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr)
        return 0;
    auto *base = static_cast<const uint8_t *>(tooltipObj);

    // Resolve the currently-displayed item (link-path itemID, or the
    // CGItem-path GUID when no unit/GO/spell is shown) — see
    // Item::TooltipItem::CurrentID for the stale-GUID guarding.
    uint64_t itemGuid = 0;
    const int itemID = Item::TooltipItem::CurrentID(base, &itemGuid);
    if (itemID <= 0)
        return 0;
    const uint32_t guidLo = static_cast<uint32_t>(itemGuid);
    const uint32_t guidHi = static_cast<uint32_t>(itemGuid >> 32);

    if (guidLo != 0 || guidHi != 0) {
        if (void *cgItem = ResolveItemByGuid(guidLo, guidHi)) {
            const char *link = Item::Link::FromCGItem(
                static_cast<const uint8_t *>(cgItem));
            if (link != nullptr && *link != '\0') {
                // Return the dressed (random-suffixed) name the engine
                // wrote into the link's `[Name]` slot — built off the same
                // CGItem via Item::Link::NameFromCGItem — so the returned
                // name agrees with the link ("Iridium Chain of the Owl",
                // not the base "Iridium Chain") and matches modern
                // GameTooltip:GetItem() on suffixed items.
                char name[128];
                if (Item::Link::NameFromCGItem(
                        static_cast<const uint8_t *>(cgItem), name, sizeof(name))) {
                    Game::Lua::PushString(L, name);
                    Game::Lua::PushString(L, link);
                    Game::Lua::PushNumber(L, static_cast<double>(itemID));
                    return 3;
                }
            }
        }
        // Fall through to the basic-link path if GUID resolve or link
        // build failed for any reason — better to return *something*
        // than nothing when we have an itemID.
    }

    // No-GUID path (SetItemByID, SetHyperlink, SetAuctionItem — items with
    // no CGItem instance): build the colored link from cached itemID +
    // quality + name. Auction/compare items have no GUID but do carry a
    // random-property (suffix) id in the compare descriptor at
    // OFF_TOOLTIP_COMPARE_SUFFIX — include it (in the link's suffix field)
    // when the compare-descriptor flag is set, so the link round-trips the
    // suffix for stat comparison. Matches 3.3.5's auction GetItem.
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const char *name = Game::Read<const char *>(
        record, Offsets::OFF_ITEMSTATS_NAME);
    if (name == nullptr || *name == '\0')
        return 0;
    const uint32_t quality = Game::Read<uint32_t>(
        record, Offsets::OFF_ITEMSTATS_QUALITY);

    int suffix = 0;
    if (Game::Read<int>(base, Offsets::OFF_TOOLTIP_COMPARE_FLAG) != 0)
        suffix = Game::Read<int>(base, Offsets::OFF_TOOLTIP_COMPARE_SUFFIX);

    char link[256];
    std::snprintf(link, sizeof(link),
                  "%s|Hitem:%d:0:%d:0:0:0:0:0|h[%s]|h|r",
                  Item::QualityColor::Prefix(static_cast<int>(quality)),
                  itemID, suffix, name);

    Game::Lua::PushString(L, name);
    Game::Lua::PushString(L, link);
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 3;
}

// `GameTooltip:HasItem()` — boolean companion to `GetItem`. Returns
// true iff the tooltip is currently showing an item (any path:
// SetBagItem, SetHyperlink, SetItemByID, SetMerchantItem, etc.). Same
// `[tooltip + OFF_TOOLTIP_ITEM_ID]` check `GetItem` does — that field
// is non-zero only while an item tooltip is live.
static int __fastcall Script_GameTooltipHasItem(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:HasItem()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    Game::Lua::PushBoolean(L, Item::TooltipItem::CurrentID(tooltipObj, nullptr) > 0);
    return 1;
}

// `GameTooltip:SetItemByGUID(guidString)` — modern method that renders
// the tooltip for the specific item instance identified by GUID
// (`"0xHHHHHHHHLLLLLLLL"` per `C_Item.GetItemGUID`). Distinct from
// `SetItemByID`, which shows the base `ItemSparse` data with no
// per-instance state: this path uses the live CGItem so the tooltip
// includes enchant lines, random-suffix-decorated name + bonuses,
// locked/broken state, etc.
//
// Implementation: parse the GUID string, resolve via the engine's
// `FUN_OBJECT_RESOLVE_BY_GUID` (same path `C_Item.GetItemLocation`
// and `GameTooltip:GetItem` use), build the fully-decorated
// hyperlink via `Item::Link::FromCGItem` (engine helper at
// `FUN_GAMETOOLTIP_BUILD_ITEM_LINK`), then dispatch to the engine's
// existing `Script_GameTooltip_SetHyperlink`. SetHyperlink parses
// the link's `item:N:enchant:gem:gem:gem:gem:suffix:unique` payload
// back into instance-specific tooltip data — closing the loop
// without us needing a separate engine entry point for "tooltip
// from CGItem*".
//
// Silent no-op when the GUID is malformed, not loaded in the
// client, or doesn't resolve to an item (creature/object GUIDs go
// to a different resolver).
static int __fastcall Script_GameTooltipSetItemByGUID(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetItemByGUID(itemGUID)");
        return 0;
    }
    if (Game::Lua::Type(L, 2) != Game::Lua::TYPE_STRING) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetItemByGUID(itemGUID)");
        return 0;
    }
    const char *guidStr = Game::Lua::ToString(L, 2);
    uint64_t guid = 0;
    if (!Item::Location::ParseGUIDString(guidStr, &guid))
        return 0;
    const uint8_t *cgItem = Item::Location::ResolveByGUID(guid);
    if (cgItem == nullptr)
        return 0;
    const char *link = Item::Link::FromCGItem(cgItem);
    if (link == nullptr || *link == '\0')
        return 0;

    // `Item::Link::FromCGItem` returns the full
    // `|cffXXXXXXXX|Hitem:N:E:S:S:S:S:R:U|h[Name]|h|r` decorated link
    // (the public-link form addons paste into chat). `SetHyperlink`
    // wants just the bare `item:N:E:S:S:S:S:R:U` payload — passing the
    // decorated form trips its "Unknown link type" Lua error because
    // the parser sees `|c` first and gives up. Extract the payload by
    // finding `|Hitem:` and copying through to the closing `|h`.
    const char *open = std::strstr(link, "|Hitem:");
    if (open == nullptr)
        return 0;
    open += 2; // skip past `|H` so we start at `item:`
    const char *close = std::strstr(open, "|h");
    if (close == nullptr)
        return 0;
    const size_t payloadLen = static_cast<size_t>(close - open);
    char payload[128];
    if (payloadLen + 1 > sizeof(payload))
        return 0;
    std::memcpy(payload, open, payloadLen);
    payload[payloadLen] = '\0';

    // Replace stack[2] (the GUID) with the bare item link and
    // dispatch to SetHyperlink with (self, "item:...").
    Game::Lua::SetTop(L, 1);
    Game::Lua::PushString(L, payload);

    using Script_t = int(__fastcall *)(void *L);
    auto fn = reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_HYPERLINK);
    return fn(L);
}

static const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetItemByID", &Script_GameTooltipSetItemByID},
    {"SetItemByGUID", &Script_GameTooltipSetItemByGUID},
    {"SetInventoryItemByID", &Script_GameTooltipSetInventoryItemByID},
    {"GetItem", &Script_GameTooltipGetItem},
    {"HasItem", &Script_GameTooltipHasItem},
};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Tooltip
