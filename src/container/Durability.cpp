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

// `C_Container.GetContainerItemDurability(containerIndex, slotIndex)` —
// modern positional-arg accessor. Same `(current, max)` shape as the
// global `GetInventoryItemDurability`, but for bag slots instead of
// the character pane. Goes through `Item::Location::ResolveBag` →
// engine's `PackBagSlot` → `GetItemBySlot` (same chain
// `C_Container.GetContainerItemID` uses). The descriptor read itself
// is the shared `Item::Durability::PushDurabilityForItem` helper from
// [[item/Durability.h]].

#include "Game.h"
#include "item/Durability.h"
#include "item/Location.h"

namespace Container::Durability {

namespace {

int __fastcall Script_C_Container_GetContainerItemDurability(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerItemDurability(containerIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));
    return Item::Durability::PushDurabilityForItem(
        L, Item::Location::ResolveBag(L, bagID, slotIndex));
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("containerIndex", "BagIndex"),
    Game::Doc::Req("slotIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("durability", "number", nullptr,
                   "Nil when the slot is empty or the item has no durability."),
    Game::Doc::Opt("maxDurability", "number"),
};
const Game::Doc::Function kGetContainerItemDurability{
    "Returns the current and maximum durability of the item in a bag slot.", kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerItemDurability",
                                     &Script_C_Container_GetContainerItemDurability,
                                     &kGetContainerItemDurability);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::Durability
