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

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::Bound {

static bool ItemIsSoulbound(const uint8_t *item) {
    auto *descriptor = Item::ObjectFields(item);
    if (descriptor == nullptr)
        return false;
    const uint32_t flags = *reinterpret_cast<const uint32_t *>(
        descriptor + Offsets::OFF_DESCRIPTOR_FLAGS);
    return (flags & Offsets::ITEM_FLAG_SOULBOUND) != 0;
}

static int __fastcall Script_IsBound(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.IsBound(itemLocation)");
        return 0;
    }

    const uint8_t *item = Item::Location::Resolve(L, 1);
    const bool bound = (item != nullptr) && ItemIsSoulbound(item);
    Game::Lua::PushBool(L, bound);
    return 1;
}

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isBound", "bool", "False for an empty slot or a location that names no item."),
};
static const Game::Doc::Function kIsBound{"Returns whether the item is soulbound.", kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "IsBound", &Script_IsBound, &kIsBound);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Bound
