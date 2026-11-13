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

#include "Link.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"
#include "item/QualityColor.h"
#include "item/Record.h"

#include <cstdint>
#include <cstdio>

namespace Item::Link {

namespace {

using BuildItemLink_t = const char *(__fastcall *)(const void *cgItem);

using BuildInstanceName_t = void(__thiscall *)(const void *cgItem, char *out,
                                               unsigned outSize);

// Inner name builder, no CGItem needed: (out, outSize, itemID, suffixID).
using BuildNameFromID_t = void(__fastcall *)(char *out, unsigned outSize,
                                             uint32_t itemID, int suffixID);

} // namespace

const char *FromCGItem(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return nullptr;
    auto fn = reinterpret_cast<BuildItemLink_t>(Offsets::FUN_GAMETOOLTIP_BUILD_ITEM_LINK);
    return fn(cgItem);
}

bool NameFromCGItem(const uint8_t *cgItem, char *out, size_t outSize) {
    if (cgItem == nullptr || out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    auto fn = reinterpret_cast<BuildInstanceName_t>(
        Offsets::FUN_ITEM_BUILD_INSTANCE_NAME);
    fn(cgItem, out, static_cast<unsigned>(outSize));
    return out[0] != '\0';
}

bool NameFromIDSuffix(uint32_t itemID, int suffixID, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    auto fn = reinterpret_cast<BuildNameFromID_t>(
        Offsets::FUN_ITEM_BUILD_NAME_FROM_ID);
    fn(out, static_cast<unsigned>(outSize), itemID, suffixID);
    return out[0] != '\0';
}

bool BasicFromIDSuffix(uint32_t itemID, int suffixID, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record == nullptr)
        return false;
    char name[128];
    if (!NameFromIDSuffix(itemID, suffixID, name, sizeof(name))) {
        // Suffix builder yielded nothing (e.g. suffix row missing) — fall back
        // to the base record name so we at least produce a valid link.
        const char *base = *reinterpret_cast<const char *const *>(
            record + Offsets::OFF_ITEMSTATS_NAME);
        if (base == nullptr || *base == '\0')
            return false;
        std::snprintf(name, sizeof(name), "%s", base);
    }
    const uint32_t quality = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_QUALITY);
    const int n = std::snprintf(out, outSize,
        "%s|Hitem:%u:0:%d:0|h[%s]|h|r",
        Item::QualityColor::Prefix(static_cast<int>(quality)),
        itemID, suffixID, name);
    return n > 0 && static_cast<size_t>(n) < outSize;
}

bool BasicFromItemID(uint32_t itemID, char *out, size_t outSize) {
    return BasicFromIDSuffix(itemID, 0, out, outSize);
}

namespace {

// `C_Item.GetItemLink(itemLocation)` — returns the fully-decorated
// per-instance hyperlink for the item at the location, matching what
// `GetContainerItemLink(bag, slot)` or `GetInventoryItemLink("player",
// slot)` would return for the same slot. Enchant ID, random-suffix,
// and any other per-instance data the server attached to the CGItem
// are preserved.
//
// Accepts table form (`{equipmentSlotIndex=N}` or `{bagID=B,
// slotIndex=S}`) and GUID string form (the value
// `C_Item.GetItemGUID` returns). `Item::Location::Resolve` handles
// all three input shapes and returns a CGItem pointer for whichever
// matches; we forward that to `FromCGItem`, which calls the engine's
// own link builder. No Lua-stack-stomping, no engine Script dispatch.
int __fastcall Script_C_Item_GetItemLink(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemLink(itemLocation)");
        return 0;
    }

    const uint8_t *cgItem = Item::Location::Resolve(L, 1);
    const char *link = FromCGItem(cgItem);
    if (link == nullptr || *link == '\0')
        return 0;

    Game::Lua::PushString(L, link);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kGetItemLinkArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
const Game::Doc::Field kGetItemLinkRets[] = {
    Game::Doc::Opt("itemLink", "string", nullptr,
                   "Nil for an empty slot or a location that names no item."),
};
const Game::Doc::Function kGetItemLink{
    "Returns the link of the item at a location, with its enchant and random suffix.",
    kGetItemLinkArgs, kGetItemLinkRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemLink", &Script_C_Item_GetItemLink,
                                     &kGetItemLink);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::Link
