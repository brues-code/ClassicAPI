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

// `C_Container.GetContainerItemID(bagIndex, slotIndex)` — modern
// positional-arg accessor for the same data
// `C_Item.GetItemID({bagID=B, slotIndex=S})` returns. Both go through
// `Item::Location::ResolveBag` → engine `PackBagSlot` →
// `GetItemBySlot` → CGItem → instance block → itemID. Returns nil for
// empty slots or invalid bag indices.

#include "Game.h"
#include "item/ID.h"
#include "item/Location.h"

namespace Container::ID {

namespace {

int __fastcall Script_C_Container_GetContainerItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: C_Container.GetContainerItemID(bagIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));

    const int itemID = Item::ID::FromCGItem(Item::Location::ResolveBag(L, bagID, slotIndex));
    if (itemID == 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("containerIndex", "BagIndex"),
    Game::Doc::Req("slotIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr, "Nil when the slot is empty."),
};
const Game::Doc::Function kGetContainerItemID{"Returns the ID of the item in a bag slot.",
                                              kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerItemID",
                                     &Script_C_Container_GetContainerItemID, &kGetContainerItemID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::ID
