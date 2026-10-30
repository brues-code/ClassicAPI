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

// `C_Container.IsContainerItemOpenable(bag, slot)` — positional-arg
// wrapper around `Item::Openable::PushIsItemOpenable`. Same chain
// `C_Container.GetContainerItemDurability` and friends use:
// `ResolveBag` → `GetItemBySlot`. Returns the
// `(isOpenable, canOpen)` tuple — see [[item/Openable.h]].

#include "Game.h"
#include "item/Location.h"
#include "item/Openable.h"

namespace Container::Openable {

namespace {

int __fastcall Script_C_Container_IsContainerItemOpenable(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.IsContainerItemOpenable(containerIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));
    return Item::Openable::PushIsItemOpenable(
        L, Item::Location::ResolveBag(L, bagID, slotIndex));
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("containerIndex", "BagIndex"),
    Game::Doc::Req("slotIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("isOpenable", "bool", nullptr,
                   "Nil when the slot is empty or the item's data is not cached yet."),
    Game::Doc::Opt("canOpen", "bool", nullptr, "False while the item is still locked."),
};
const Game::Doc::Function kIsContainerItemOpenable{
    "Returns whether the item in a bag slot can be opened, and whether the player can "
    "open it now.",
    kArgs, kRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "IsContainerItemOpenable",
                                     &Script_C_Container_IsContainerItemOpenable,
                                     &kIsContainerItemOpenable);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::Openable
