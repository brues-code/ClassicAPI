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

// `C_Item.GetStackCount(itemLocation)` — current stack count for the
// specific item slot. Reads `ITEM_FIELD_STACK_COUNT` directly off the
// CGItem's m_objectFields descriptor — same field
// `Script_GetContainerItemInfo` returns as `itemCount`. Distinct from
// `C_Item.GetItemCount(itemID)`, which sums every stack of `itemID`
// across the player's inventory.

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::StackCount {

static int __fastcall Script_GetStackCount(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    auto *item = Item::Location::Resolve(L, 1);
    if (item == nullptr) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    auto *descriptor = Item::ObjectFields(item);
    if (descriptor == nullptr) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    const int count = *reinterpret_cast<const int *>(
        descriptor + Offsets::OFF_DESCRIPTOR_STACK_COUNT);
    Game::Lua::PushNumber(L, static_cast<double>(count));
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Req("stackCount", "number",
                   "0 for an empty slot or a location that names no item."),
};
static const Game::Doc::Function kGetStackCount{
    "Returns how many items are in the stack at a location.", kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetStackCount",
                                     &Script_GetStackCount, &kGetStackCount);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::StackCount
