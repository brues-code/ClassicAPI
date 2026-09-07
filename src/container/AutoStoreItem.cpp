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

// `C_Container.AutoStoreItem(srcBag, srcSlot [, dstBag])` — move an item
// and let the SERVER choose the destination slot. A ClassicAPI-only
// extension, like its siblings `SwapItems` and `MoveItem`; the modern
// API has no equivalent, because it has no need for one.
//
// The reason to want it: the server does not just find an empty slot.
// It first merges the stack into existing non-full stacks of the same
// item, splitting it across several of them when that is what fits, and
// only places a remainder if any is left over. So this is a one-call
// stack consolidator that needs no stack-size lookup — the caller never
// has to know what fits. A merge computed on this side has to size
// every stack first, and `C_Item.GetItemMaxStackSizeByID` is nil until
// that item's data has arrived, so a cold item cache silently skips
// exactly the stacks it could not size.
//
// `dstBag` defaults to 0, which means "anywhere in the main inventory
// that it fits" rather than "the backpack" — on the wire the backpack
// IS the player container, so the two cannot be distinguished. Pass
// 1..4 to confine the item to one equipped bag. Bank destinations are
// rejected (see [[item/Swap.h]] `AutoStore`).
//
// Returns true once the packet is away — like the other two, the send
// is fire-and-forget and the server confirms through the normal
// BAG_UPDATE / SMSG_INVENTORY_CHANGE_FAILURE flow.

#include "Game.h"
#include "item/Swap.h"

namespace Container::AutoStoreItem {

namespace {

int __fastcall Script_C_Container_AutoStoreItem(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L,
            "Usage: C_Container.AutoStoreItem(srcBag, srcSlot [, dstBag])");
        return 0;
    }
    // Read every arg before calling AutoStore — its source lookup goes
    // through ResolveBag, which stomps the Lua stack via PackBagSlot.
    const int srcBag = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int srcSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int dstBag = Game::Lua::IsNumber(L, 3)
                           ? static_cast<int>(Game::Lua::ToNumber(L, 3))
                           : 0;

    const bool ok = Item::Swap::AutoStore(L, srcBag, srcSlot, dstBag);
    Game::Lua::PushBool(L, ok);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "AutoStoreItem",
                                     &Script_C_Container_AutoStoreItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::AutoStoreItem
