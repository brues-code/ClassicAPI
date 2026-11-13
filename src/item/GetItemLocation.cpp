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
#include "item/Location.h"

#include <cstdint>

namespace Item::GetItemLocation {

namespace {

void PushEquipmentLocation(void *L, int slot) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "equipmentSlotIndex",
                              static_cast<double>(slot));
}

void PushBagLocation(void *L, int bagID, int slotIndex) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "bagID", static_cast<double>(bagID));
    Game::Lua::SetFieldNumber(L, "slotIndex", static_cast<double>(slotIndex));
}

} // namespace

// `C_Item.GetItemLocation(itemGUID)` — reverse lookup from a per-
// instance item GUID (in the `"0xHHHHHHHHLLLLLLLL"` form
// `C_Item.GetItemGUID` returns) to an `itemLocation` table that
// identifies the slot currently holding it. Returns `nil` for
// unknown / non-resident GUIDs.
//
// Modern WoW returns an `ItemLocation` mixin object; we return a
// plain table with the same field shape every other `C_Item.*` API
// in ClassicAPI accepts as input (`{equipmentSlotIndex=N}` or
// `{bagID=B, slotIndex=S}`), so the result pipes straight into
// `C_Item.GetItemQuality(loc)`, `C_Item.GetItemLink(loc)`, etc.
//
// Walked scope: character-pane equipment (slots 1..19) + backpack
// (bagID 0) + the four equipped bags (bagIDs 1..4). Bank and keyring
// are NOT walked — same scope as the rest of the `C_Container.*`
// family.
static int __fastcall Script_C_Item_GetItemLocation(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_STRING) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemLocation(itemGUID)");
        return 0;
    }
    uint64_t guid = 0;
    if (!Item::Location::ParseGUIDString(Game::Lua::ToString(L, 1), &guid))
        return 0;

    Item::Location::ByGUIDResult found;
    if (!Item::Location::FindByGUID(L, guid, &found))
        return 0;

    if (found.equipmentSlotIndex != 0)
        PushEquipmentLocation(L, found.equipmentSlotIndex);
    else
        PushBagLocation(L, found.bagID, found.slotIndex);
    return 1;
}

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("itemGUID", "WOWGUID"),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemLocation", "ItemLocation", nullptr,
                   "A plain table, not an ItemLocation object; nil when the player does not "
                   "wear the item or carry it in the bags."),
};
static const Game::Doc::Function kGetItemLocation{
    "Returns where the item with that GUID is now.", kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemLocation",
                                     &Script_C_Item_GetItemLocation, &kGetItemLocation);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::GetItemLocation
