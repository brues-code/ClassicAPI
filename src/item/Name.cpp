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
#include "item/Arg.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Link.h"
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>

namespace Item::Name {

namespace {

// Reads `m_name[0]` directly off a cached record. Items only ever
// populate the first name slot — the array shape (`char *m_name[4]`
// per VanillaHelpers's `ItemStats_C`) is a client-internal layout, not
// a locale array like `Spell.dbc`. The active locale's name is what
// the server delivered in `SMSG_ITEM_QUERY_SINGLE_RESPONSE`.
int PushNameForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const char *name = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMSTATS_NAME);
    if (name == nullptr || *name == '\0')
        return 0;
    Game::Lua::PushString(L, name);
    return 1;
}

// Pushes the suffix-decorated display name for a resolved `CGItem` via
// the shared engine name builder. Returns 0 (pushes nothing) for a null
// item or an empty build result, so the caller can fall back to the
// base-name-by-itemID path.
int PushInstanceName(void *L, const uint8_t *cgItem) {
    char buf[128];
    if (!Item::Link::NameFromCGItem(cgItem, buf, sizeof(buf)))
        return 0;
    Game::Lua::PushString(L, buf);
    return 1;
}

} // namespace

// `C_Item.GetItemName(itemLocation)` — returns the display name of the
// item at the location, or nil for empty / uncached / invalid inputs.
// The location points at a real item instance, so the name carries any
// random-suffix decoration ("... of the Sorcerer") — built off the
// CGItem via the engine's own name builder, matching modern WoW and
// `C_Item.GetItemLink`'s bracketed name. Falls back to the base
// `ItemStats_C` name (and warms the cache) if the instance build yields
// nothing.
static int __fastcall Script_C_Item_GetItemName(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemName(itemLocation)");
        return 0;
    }
    const uint8_t *cgItem = Item::Location::Resolve(L, 1);
    if (int pushed = PushInstanceName(L, cgItem))
        return pushed;
    return PushNameForItemID(L, Item::ID::FromCGItem(cgItem));
}

// `C_Item.GetItemNameByID(itemInfo)` — returns the cached display name
// for `itemID`, firing a background cache fill on miss. Accepts the
// same input shapes as `GetItemInfoInstant` (number, `"item:NNN..."`
// string, or any string containing a parseable `item:NN` chunk).
//
// When the input is an item link carrying a random suffix
// (`item:12022:0:769:0`), the returned name is suffix-decorated
// ("Iridium Chain of the Owl") — the 3rd `item:` field is parsed and
// applied via the engine's own name builder, so it matches the global
// `GetItemInfo`'s name. A bare itemID (number or `item:N`) has no suffix
// and yields the base name.
static int __fastcall Script_C_Item_GetItemNameByID(void *L) {
    const Item::Arg::Resolved r = Item::Arg::Resolve(L, 1);
    if (r.itemID <= 0)
        return 0;
    char buf[128];
    if (Item::Link::NameFromIDSuffix(static_cast<uint32_t>(r.itemID), r.suffix,
                                     buf, sizeof(buf))) {
        Game::Lua::PushString(L, buf);
        return 1;
    }
    // Not cached yet — fire a background fill and return nil (same miss
    // behavior as the base-name path).
    Item::Data::WarmCache(static_cast<uint32_t>(r.itemID));
    return 0;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kGetItemNameArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kGetItemNameRets[] = {
    Game::Doc::Opt("itemName", "string", nullptr,
                   "Includes the random suffix; nil for an empty slot or an item that is not "
                   "cached."),
};
static const Game::Doc::Function kGetItemName{
    "Returns the name of the item at a location.", kGetItemNameArgs, kGetItemNameRets};

static const Game::Doc::Field kGetItemNameByIDArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kGetItemNameByIDRets[] = {
    Game::Doc::Opt("itemName", "string", nullptr,
                   "Includes the random suffix of a link; nil while the item is not cached."),
};
static const Game::Doc::Function kGetItemNameByID{
    "Returns the name of an item, and asks the server for an item that is not cached.",
    kGetItemNameByIDArgs, kGetItemNameByIDRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemName", &Script_C_Item_GetItemName,
                                     &kGetItemName);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemNameByID", &Script_C_Item_GetItemNameByID,
                                     &kGetItemNameByID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Name
