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

// `C_Container.SwapItems(srcBag, srcSlot, dstBag, dstSlot)` — atomic
// server-side swap of two bag slots, no cursor involvement. This is a
// ClassicAPI-only extension, not a modern API mirror; modern Classic
// has no direct swap-two-slots call (addons there string two
// `PickupContainerItem` calls together to drive the cursor). Same
// engine primitive `C_EquipmentSet.UseEquipmentSet` and our paperdoll-
// swap path use — see [[item/Swap.h]] `Item::Swap::Containers`.
//
// Returns true on send, false on bad args (missing bag, out-of-range
// slot, empty source). Send is fire-and-forget — server confirms via
// the normal SMSG_INVENTORY_CHANGE_FAILURE / BAG_UPDATE flow.

#include "Game.h"
#include "item/Swap.h"

namespace Container::SwapItems {

namespace {

int __fastcall Script_C_Container_SwapItems(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L,
            "Usage: C_Container.SwapItems(srcBag, srcSlot, dstBag, dstSlot)");
        return 0;
    }
    // Read all four args before invoking Containers — its internal
    // ResolveBag stomps the Lua stack via PackBagSlot.
    const int srcBag = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int srcSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int dstBag = static_cast<int>(Game::Lua::ToNumber(L, 3));
    const int dstSlot = static_cast<int>(Game::Lua::ToNumber(L, 4));

    const bool ok = Item::Swap::Containers(L, srcBag, srcSlot, dstBag, dstSlot);
    Game::Lua::PushBool(L, ok);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("srcBag", "BagIndex"),
    Game::Doc::Req("srcSlot", "luaIndex"),
    Game::Doc::Req("dstBag", "BagIndex"),
    Game::Doc::Req("dstSlot", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("sent", "bool", "False when an argument is not valid."),
};
const Game::Doc::Function kSwapItems{
    "Swaps the contents of two bag slots without the cursor.", kArgs, kRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "SwapItems", &Script_C_Container_SwapItems,
                                     &kSwapItems);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::SwapItems
