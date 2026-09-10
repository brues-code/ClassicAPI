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

// `C_Container.GetContainerItemInfo(containerIndex, slotIndex)` — the modern
// structured-table accessor (namespaced + table-returning since Patch
// 10.0.2). Vanilla's global `GetContainerItemInfo` returns a flat 5-tuple
// (`texture, itemCount, locked, quality, readable` — verified by decoding
// `Script_GetContainerItemInfo` at `0x004F9670`); backported addons expect
// the `ContainerItemInfo` table with the wider modern field set. We assemble
// that table from the same sources the engine reads plus the extra fields it
// never surfaced in 1.12.
//
// Field sources:
//   iconFileID  — ItemStats displayInfoID → ItemDisplayInfo.dbc icon path.
//                 (1.12 has no fileID system; we return the icon *path*
//                 string, same convention as `GetItemIcon`.)
//   stackCount  — CGItem descriptor `+OFF_DESCRIPTOR_STACK_COUNT` (the
//                 engine's `itemCount` return).
//   isLocked    — CGItem `+OFF_ITEM_CLIENT_LOCK & 1` (the `locked` return).
//   quality     — ItemStats `+OFF_ITEMSTATS_QUALITY`; nil when uncached
//                 (modern contract: `Enum.ItemQuality or nil`).
//   isReadable  — static page text OR the per-instance readable-text id,
//                 mirroring the engine's `readable` branch at `0x004F97BD`.
//   hasLoot     — ItemStats LOOTABLE flag (best-effort static approximation:
//                 vanilla can't track post-loot dynamic emptiness client-side).
//   hyperlink   — engine link builder via `Item::Link::FromCGItem` (enchant /
//                 random-suffix decorated, same as `GetContainerItemLink`).
//   isFiltered  — always false (vanilla has no bag search-filter system).
//   hasNoValue  — ItemStats sellPrice == 0.
//   itemID      — instance block itemID (`Item::ID::FromCGItem`).
//   isBound     — CGItem descriptor SOULBOUND flag (`C_Item.IsBound`'s bit).
//   itemName    — instance-decorated display name, base ItemStats name on miss.
//
// Returns nil for an empty slot or an invalid bag/slot index (modern
// contract). Cache-derived fields (icon / quality / hasLoot / hasNoValue /
// name) are omitted/nil when the item's static data isn't cached yet — the
// live per-instance fields (count / locked / bound / itemID / link) are always
// present. Passive: does not warm the item cache.

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Icon.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/StatAccum.h"

#include <cstdint>

namespace Container::ItemInfo {

namespace {

// Mirrors the engine's `readable` decision (0x004F97BD..0x004F97CF): a slot
// is readable when its static page text is set OR the per-instance
// readable-text id is non-zero (letters / server-authored notes).
bool ItemReadable(const uint8_t *item, const uint8_t *record) {
    if (record != nullptr &&
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_PAGE_TEXT) != 0)
        return true;
    const uint8_t *descriptor = Item::ObjectFields(item);
    return descriptor != nullptr &&
           Game::Read<uint32_t>(
               descriptor, Offsets::OFF_DESCRIPTOR_READABLE_TEXT_ID) != 0;
}

int __fastcall Script_C_Container_GetContainerItemInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerItemInfo(containerIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));

    const uint8_t *item = Item::Location::ResolveBag(L, bagID, slotIndex);
    if (item == nullptr) {
        Game::Lua::PushNil(L); // empty slot / bad index → nil (modern contract)
        return 1;
    }

    // --- Gather everything into locals BEFORE touching the Lua stack. The
    // engine link/name builders and cache/DBC reads below are pure C; keeping
    // them off the half-built table avoids any stack interleaving. ---
    const int itemID = Item::ID::FromCGItem(item);
    const uint8_t *record = (itemID > 0)
        ? Item::PeekRecord(static_cast<uint32_t>(itemID))
        : nullptr;

    // Live per-instance descriptor fields.
    const uint8_t *descriptor = Item::ObjectFields(item);
    int stackCount = 0;
    bool isBound = false;
    if (descriptor != nullptr) {
        stackCount = Game::Read<int>(
            descriptor, Offsets::OFF_DESCRIPTOR_STACK_COUNT);
        isBound = (Game::Read<uint32_t>(
                       descriptor, Offsets::OFF_DESCRIPTOR_FLAGS) &
                   Offsets::ITEM_FLAG_SOULBOUND) != 0;
    }
    const bool isLocked = (Game::Read<uint32_t>(
                               item, Offsets::OFF_ITEM_CLIENT_LOCK) &
                           Offsets::ITEM_CLIENT_LOCK_BIT) != 0;
    const bool isReadable = ItemReadable(item, record);

    // Static template fields (cache-dependent).
    bool haveIcon = false;
    char iconPath[260];
    bool haveQuality = false;
    uint32_t quality = 0;
    bool hasLoot = false;
    bool hasNoValue = false;
    if (record != nullptr) {
        const uint32_t displayInfoID = Game::Read<uint32_t>(
            record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
        haveIcon = Item::Icon::PathForDisplayInfoID(displayInfoID, iconPath, sizeof(iconPath));
        quality = Game::Read<uint32_t>(
            record, Offsets::OFF_ITEMSTATS_QUALITY);
        haveQuality = true;
        const uint32_t flags = Game::Read<uint32_t>(
            record, Offsets::OFF_ITEMSTATS_FLAGS);
        hasLoot = (flags & Offsets::ITEM_FLAG_LOOTABLE) != 0;
        hasNoValue = Game::Read<uint32_t>(
                         record, Offsets::OFF_ITEMSTATS_SELL_PRICE) == 0;
    }

    const char *hyperlink = Item::Link::FromCGItem(item);

    char nameBuf[256];
    const char *itemName = nullptr;
    if (Item::Link::NameFromCGItem(item, nameBuf, sizeof(nameBuf))) {
        itemName = nameBuf;
    } else if (record != nullptr) {
        const char *baseName = Game::Read<const char *>(
            record, Offsets::OFF_ITEMSTATS_NAME);
        if (baseName != nullptr && baseName[0] != '\0')
            itemName = baseName;
    }

    // --- Build the ContainerItemInfo table (only stack-safe SetField* now). ---
    Game::Lua::NewTable(L);
    if (haveIcon)
        Game::Lua::SetFieldString(L, "iconFileID", iconPath);
    Game::Lua::SetFieldNumber(L, "stackCount", static_cast<double>(stackCount));
    Game::Lua::SetFieldBool(L, "isLocked", isLocked);
    if (haveQuality)
        Game::Lua::SetFieldNumber(L, "quality", static_cast<double>(quality));
    Game::Lua::SetFieldBool(L, "isReadable", isReadable);
    Game::Lua::SetFieldBool(L, "hasLoot", hasLoot);
    if (hyperlink != nullptr && hyperlink[0] != '\0')
        Game::Lua::SetFieldString(L, "hyperlink", hyperlink);
    Game::Lua::SetFieldBool(L, "isFiltered", false);
    Game::Lua::SetFieldBool(L, "hasNoValue", hasNoValue);
    Game::Lua::SetFieldNumber(L, "itemID", static_cast<double>(itemID));
    Game::Lua::SetFieldBool(L, "isBound", isBound);
    if (itemName != nullptr)
        Game::Lua::SetFieldString(L, "itemName", itemName);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerItemInfo",
                                     &Script_C_Container_GetContainerItemInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::ItemInfo
