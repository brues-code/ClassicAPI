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
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>

namespace Item::MaxStackSize {

namespace {

int PushMaxStackSizeForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const uint32_t maxStack = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_STACKABLE);
    Game::Lua::PushNumber(L, static_cast<double>(maxStack));
    return 1;
}

} // namespace

// `C_Item.GetItemMaxStackSize(itemLocation)` — max stack size for
// the item type at the given location. Returns the type's stack
// cap (e.g. 20 for Linen Cloth) regardless of how many are
// actually in the slot; for the current stack count of a specific
// item, use `C_Item.GetStackCount`. Non-stackable items return 1.
// nil for empty / uncached / invalid locations.
static int __fastcall Script_C_Item_GetItemMaxStackSize(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemMaxStackSize(itemLocation)");
        return 0;
    }
    return PushMaxStackSizeForItemID(L, Item::ID::FromCGItem(Item::Location::Resolve(L, 1)));
}

// `C_Item.GetItemMaxStackSizeByID(itemInfo)` — same data, but
// keyed on itemID (or `"item:NNN..."` link string). For uncached
// items, returns nil and fires a background cache fill — re-call
// after `GET_ITEM_INFO_RECEIVED` for the value.
static int __fastcall Script_C_Item_GetItemMaxStackSizeByID(void *L) {
    return PushMaxStackSizeForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kLocationArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kByIDArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kLocationRets[] = {
    Game::Doc::Opt("stackSize", "number", nullptr,
                   "1 for an item that does not stack; nil for an empty slot or an item that "
                   "is not cached."),
};
static const Game::Doc::Field kByIDRets[] = {
    Game::Doc::Opt("stackSize", "number", nullptr,
                   "1 for an item that does not stack; nil while the item is not cached."),
};
static const Game::Doc::Function kGetItemMaxStackSize{
    "Returns how many of the item at a location fit in one stack.", kLocationArgs,
    kLocationRets};
static const Game::Doc::Function kGetItemMaxStackSizeByID{
    "Returns how many of an item fit in one stack, and asks the server for an item that is "
    "not cached.",
    kByIDArgs, kByIDRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemMaxStackSize",
                                     &Script_C_Item_GetItemMaxStackSize, &kGetItemMaxStackSize);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemMaxStackSizeByID",
                                     &Script_C_Item_GetItemMaxStackSizeByID,
                                     &kGetItemMaxStackSizeByID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::MaxStackSize
