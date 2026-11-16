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
#include "item/ID.h"
#include "item/Location.h"
#include "object/Resolve.h"

#include <cstdint>

namespace Item::ID {

int FromCGItem(const uint8_t *item) {
    return static_cast<int>(::Object::Entry(item));
}

const uint8_t *VisibleEntry(void *player, int slot0) {
    using GetVisibleItem_t = const uint8_t *(__thiscall *)(void *unit, int slot);
    return reinterpret_cast<GetVisibleItem_t>(Offsets::FUN_UNIT_GET_VISIBLE_ITEM)(player, slot0);
}

uint32_t Visible(void *player, int slot0) {
    const uint8_t *entry = VisibleEntry(player, slot0);
    return entry != nullptr ? Game::Read<uint32_t>(entry, Offsets::OFF_VISIBLE_ITEM_ITEM_ID) : 0;
}

static int __fastcall Script_GetItemID(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemID(itemLocation)");
        return 0;
    }

    const int itemID = FromCGItem(Item::Location::Resolve(L, 1));
    if (itemID == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil for an empty slot or a location that is not valid."),
};
static const Game::Doc::Function kGetItemID{"Returns the item ID of the item at a location.",
                                            kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemID", &Script_GetItemID, &kGetItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::ID
