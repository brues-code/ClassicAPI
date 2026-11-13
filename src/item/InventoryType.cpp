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

namespace Item::InventoryType {

namespace {

int PushInventoryTypeForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const uint32_t invType = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    Game::Lua::PushNumber(L, static_cast<double>(invType));
    return 1;
}

} // namespace

// `C_Item.GetItemInventoryType(itemLocation)` — returns the numeric
// `Enum.InventoryType` (0 = non-equip, 1 = head, 2 = neck, …, 26 =
// ranged-right, 27 = quiver) read straight off the cache record's
// `m_inventoryType` field (`+0x2C`). 1.12 only uses values 0..28 — the
// `INVTYPE_PROFESSION_*` etc. additions are post-vanilla, but modern
// backport code that compares against the higher enum values still
// works (vanilla items never report those values, so the comparisons
// resolve correctly).
//
// This is the **numeric** sibling of `GetItemInfoInstant`'s 4th return
// (the `INVTYPE_*` string). Callers that want the string already have
// `GetItemInfoInstant`; this is for modern code that expects the
// integer.
static int __fastcall Script_C_Item_GetItemInventoryType(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemInventoryType(itemLocation)");
        return 0;
    }
    return PushInventoryTypeForItemID(L, Item::ID::FromCGItem(Item::Location::Resolve(L, 1)));
}

// `C_Item.GetItemInventoryTypeByID(itemInfo)` — same as the above but
// accepts itemID number or `"item:NNN..."` string directly. Returns
// nil for uncached items, firing a background fill so a follow-up
// call after `GET_ITEM_INFO_RECEIVED` succeeds.
static int __fastcall Script_C_Item_GetItemInventoryTypeByID(void *L) {
    return PushInventoryTypeForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

static const Game::Doc::Field kLocationArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kByIDArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("inventoryType", "InventoryType", nullptr,
                   "Nil when there is no item or its data is not loaded, which also asks the "
                   "server for it."),
};
static const Game::Doc::Function kGetItemInventoryType{
    "Returns the equip location of the item at a location.", kLocationArgs, kRets};
static const Game::Doc::Function kGetItemInventoryTypeByID{
    "Returns the item's equip location.", kByIDArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInventoryType",
                                     &Script_C_Item_GetItemInventoryType, &kGetItemInventoryType);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInventoryTypeByID",
                                     &Script_C_Item_GetItemInventoryTypeByID,
                                     &kGetItemInventoryTypeByID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::InventoryType
