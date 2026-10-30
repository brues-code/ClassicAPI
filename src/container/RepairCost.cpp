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

// `C_Container.GetContainerItemRepairCost(containerIndex, slotIndex)`
// — bag-side variant of `GetInventoryItemRepairCost`. The engine's
// `RepairAllCost` (`FUN_004FBD60`) walks both equipment slots and
// bag contents through the same per-item helper, so this is the
// same copper cost a merchant would charge for that item.

#include "Game.h"
#include "item/Location.h"
#include "item/RepairCost.h"

namespace Container::RepairCost {

namespace {

int __fastcall Script_C_Container_GetContainerItemRepairCost(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerItemRepairCost(containerIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));
    return Item::RepairCost::PushRepairCostForItem(
        L, Item::Location::ResolveBag(L, bagID, slotIndex));
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("containerIndex", "BagIndex"),
    Game::Doc::Req("slotIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("repairCost", "number", "In copper; 0 when the item needs no repair."),
};
const Game::Doc::Function kGetContainerItemRepairCost{
    "Returns what a merchant charges to repair the item in a bag slot.", kArgs, kRets,
    nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerItemRepairCost",
                                     &Script_C_Container_GetContainerItemRepairCost,
                                     &kGetContainerItemRepairCost);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::RepairCost
