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

// `C_Container.HasContainerItem(bagIndex, slotIndex)` — returns whether the
// given bag slot currently holds an item (a Classic Era addition). It's the
// occupancy sibling of `GetContainerItemID`: resolves the slot to its CGItem
// the same way (`Item::Location::ResolveBag` → engine `PackBagSlot` →
// `GetItemBySlot`), and a populated slot yields a non-null pointer while an
// empty slot yields null. Presence only — it does not depend on the item's
// static data being cached. Returns `false` for empty slots and invalid
// bag/slot indices.

#include "Game.h"
#include "item/Location.h"

namespace Container::HasItem {

namespace {

int __fastcall Script_C_Container_HasContainerItem(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: C_Container.HasContainerItem(bagIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));

    const bool hasItem = Item::Location::ResolveBag(L, bagID, slotIndex) != nullptr;
    Game::Lua::PushBool(L, hasItem);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("containerIndex", "BagIndex"),
    Game::Doc::Req("slotIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("hasItem", "bool"),
};
const Game::Doc::Function kHasContainerItem{
    "Returns whether a bag slot holds an item, even one whose data is not cached yet.",
    kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "HasContainerItem",
                                     &Script_C_Container_HasContainerItem, &kHasContainerItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::HasItem
