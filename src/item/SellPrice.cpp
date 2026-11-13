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

namespace Item::SellPrice {

namespace {

int PushSellPriceForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const uint32_t sellPrice = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_SELL_PRICE);
    Game::Lua::PushNumber(L, static_cast<double>(sellPrice));
    return 1;
}

} // namespace

// `C_Item.GetItemSellPrice(itemLocation)` — direct cache read for the
// equipped/bagged item's vendor sell price (copper, per unit). Returns
// nil on empty / uncached / invalid location.
static int __fastcall Script_C_Item_GetItemSellPrice(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemSellPrice(itemLocation)");
        return 0;
    }
    return PushSellPriceForItemID(L, Item::ID::FromCGItem(Item::Location::Resolve(L, 1)));
}

// `C_Item.GetItemSellPriceByID(itemInfo)` — same as the above but
// accepts the itemID (or `"item:NNN..."` string) directly. Returns
// nil for uncached items, firing a background cache fill so a
// follow-up call after `GET_ITEM_INFO_RECEIVED` returns the value.
static int __fastcall Script_C_Item_GetItemSellPriceByID(void *L) {
    return PushSellPriceForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kLocationArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kByIDArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kLocationRets[] = {
    Game::Doc::Opt("sellPrice", "number", nullptr,
                   "In copper, for one item; nil for an empty slot or an item that is not "
                   "cached."),
};
static const Game::Doc::Field kByIDRets[] = {
    Game::Doc::Opt("sellPrice", "number", nullptr,
                   "In copper, for one item; nil while the item is not cached."),
};
static const Game::Doc::Function kGetItemSellPrice{
    "Returns what a merchant pays for one of the item at a location.", kLocationArgs,
    kLocationRets, nullptr, true};
static const Game::Doc::Function kGetItemSellPriceByID{
    "Returns what a merchant pays for one of an item, and asks the server for an item that is "
    "not cached.",
    kByIDArgs, kByIDRets, nullptr, true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSellPrice",
                                     &Script_C_Item_GetItemSellPrice, &kGetItemSellPrice);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSellPriceByID",
                                     &Script_C_Item_GetItemSellPriceByID, &kGetItemSellPriceByID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::SellPrice
