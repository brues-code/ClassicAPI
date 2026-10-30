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

// `C_Container.MoveItem(srcBag, srcSlot, dstBag, dstSlot, count)` —
// atomic split-and-place. ClassicAPI-only extension; modern Classic
// has no equivalent (addons there have to drive the cursor via
// `SplitContainerItem` + `PickupContainerItem`). Uses the same engine
// primitive `SplitContainerItem` lowers to (`CMSG_SPLIT_ITEM`, opcode
// 0x10E) but with the destination wired in, so the cursor is never
// touched.
//
// Semantics (server-enforced, no partial moves):
//   - `dst` empty                        → places `count` there
//   - `dst` same item, fits maxStack     → merges `count` into dst
//   - `dst` different item / overflow    → server rejects
//
// Returns true on send, false on bad args (missing bag, out-of-range
// slot, empty source, count < 1 or > 255). The 255 cap matches the
// vanilla wire format (count is a single packet byte) — items with
// `maxStack > 255` don't exist in 1.12.

#include "Game.h"
#include "item/Swap.h"

namespace Container::MoveItem {

namespace {

int __fastcall Script_C_Container_MoveItem(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4) ||
        !Game::Lua::IsNumber(L, 5)) {
        Game::Lua::Error(L,
            "Usage: C_Container.MoveItem(srcBag, srcSlot, dstBag, dstSlot, count)");
        return 0;
    }
    // Read all five args before Item::Swap::MoveCount — its internal
    // ResolveBag stomps the Lua stack.
    const int srcBag = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int srcSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int dstBag = static_cast<int>(Game::Lua::ToNumber(L, 3));
    const int dstSlot = static_cast<int>(Game::Lua::ToNumber(L, 4));
    const int count = static_cast<int>(Game::Lua::ToNumber(L, 5));

    const bool ok = Item::Swap::MoveCount(L, srcBag, srcSlot, dstBag, dstSlot, count);
    Game::Lua::PushBool(L, ok);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("srcBag", "BagIndex"),
    Game::Doc::Req("srcSlot", "luaIndex"),
    Game::Doc::Req("dstBag", "BagIndex"),
    Game::Doc::Req("dstSlot", "luaIndex"),
    Game::Doc::Req("count", "number", "From 1 to 255."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("sent", "bool", "False when an argument is not valid."),
};
const Game::Doc::Function kMoveItem{
    "Moves part of a stack to another bag slot without the cursor.", kArgs, kRets, nullptr,
    true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "MoveItem", &Script_C_Container_MoveItem,
                                     &kMoveItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::MoveItem
