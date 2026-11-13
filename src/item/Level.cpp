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

namespace Item::Level {

namespace {

int PushItemLevelForItemID(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const uint32_t ilvl = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_ITEM_LEVEL);
    Game::Lua::PushNumber(L, static_cast<double>(ilvl));
    return 1;
}

} // namespace

// `C_Item.GetCurrentItemLevel(itemLocation)` — base ilvl of the
// equipped/bagged item from the cache. Vanilla 1.12 has no per-instance
// scaling (no upgrades, no warforging), so "current" and "base" item
// level are the same value. Direct `m_itemLevel` read, no `GetItemInfo`
// chaining.
static int __fastcall Script_C_Item_GetCurrentItemLevel(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetCurrentItemLevel(itemLocation)");
        return 0;
    }
    return PushItemLevelForItemID(L, Item::ID::FromCGItem(Item::Location::Resolve(L, 1)));
}

// `C_Item.GetDetailedItemLevelInfo(itemInfo)` — modern returns
// `(currentLevel, isPreview, baseLevel)`. In vanilla all three values
// are identical (no upgrade/preview system), so we push the base ilvl
// as `currentLevel` and let callers ignore the missing extra returns.
// Accepts numeric itemID or `"item:NNN..."` string; nil for uncached.
static int __fastcall Script_C_Item_GetDetailedItemLevelInfo(void *L) {
    return PushItemLevelForItemID(L, Item::Arg::ResolveItemID(L, 1));
}

static const Game::Doc::Field kCurrentArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kCurrentRets[] = {
    Game::Doc::Opt("currentItemLevel", "number", nullptr,
                   "Nil when there is no item or its data is not loaded, which also asks the "
                   "server for it."),
};
static const Game::Doc::Function kGetCurrentItemLevel{
    "Returns the item level of the item at a location.", kCurrentArgs, kCurrentRets};

static const Game::Doc::Field kDetailedArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo", "An item ID or an item link."),
};
static const Game::Doc::Field kDetailedRets[] = {
    Game::Doc::Opt("actualItemLevel", "number", nullptr,
                   "The only value returned; nil when the item's data is not loaded, which also "
                   "asks the server for it."),
};
static const Game::Doc::Function kGetDetailedItemLevelInfo{
    "Returns the item's item level.", kDetailedArgs, kDetailedRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetCurrentItemLevel",
                                     &Script_C_Item_GetCurrentItemLevel, &kGetCurrentItemLevel);
    Game::Lua::RegisterTableFunction("C_Item", "GetDetailedItemLevelInfo",
                                     &Script_C_Item_GetDetailedItemLevelInfo,
                                     &kGetDetailedItemLevelInfo);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Level
