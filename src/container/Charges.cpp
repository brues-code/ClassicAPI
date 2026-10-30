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

// `C_Container.GetContainerItemCharges(containerIndex, slotIndex)` —
// returns the `abs(SPELL_CHARGES[0])` value of the item in the
// specified bag slot, or `nil` for empty slots and items that don't
// carry a charge concept. Wands return their remaining cast count
// (e.g. 50 → 49 → …); single-use items (Healthstone, Mana Gem,
// Goblin Sapper Charge) return `1`.
//
// Modern equivalent: none — modern WoW exposes charges only through
// tooltip text scanning. `C_Item.GetItemCount(includeBank, includeCharges)`
// totals charges across every matching stack; this is the per-slot
// reader.

#include "Game.h"
#include "item/Charges.h"
#include "item/Location.h"

namespace Container::Charges {

namespace {

int __fastcall Script_C_Container_GetContainerItemCharges(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.GetContainerItemCharges(containerIndex, slotIndex)");
        return 0;
    }
    const int bagID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int slotIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));
    return Item::Charges::PushChargesForItem(
        L, Item::Location::ResolveBag(L, bagID, slotIndex));
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("containerIndex", "BagIndex"),
    Game::Doc::Req("slotIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("uses", "number", nullptr, "Nil when the slot is empty."),
};
const Game::Doc::Function kGetContainerItemCharges{
    "Returns the uses left on the item in a bag slot: its charges times its stack size.",
    kArgs, kRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerItemCharges",
                                     &Script_C_Container_GetContainerItemCharges,
                                     &kGetContainerItemCharges);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::Charges
