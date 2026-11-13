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

// `C_Item.PickupItem(itemInfo)` — finds the item matching `itemInfo` in
// the player's equipment or bags and picks it up onto the cursor.
// `itemInfo` is an itemID, `"item:NNN"` string, item link, or item name —
// the same input `C_Item.UseItemByName` / `EquipItemByName` accept,
// resolved via `Item::Arg::Resolve` and matched by `FindByArg` (itemID, or
// decorated name for a name arg) across equipment (1..19, checked first)
// and bags (0..4).
//
// The pickup goes through the shared `Item::Cursor` wrappers, which call
// the engine's cursor primitives directly (no Lua `Script_Pickup*`
// wrapper): an equipped match uses the equipment-slot primitive, a bag
// match the bag primitive (guarded on an empty cursor + unlocked item).
// Silently no-ops on bad input, no match, a busy cursor, or a locked item.

#include "Game.h"
#include "item/Arg.h"
#include "item/Cursor.h"
#include "item/Location.h"

namespace Item::Pickup {

namespace {

int __fastcall Script_C_Item_PickupItem(void *L) {
    const auto arg = Item::Arg::Resolve(L, 1);
    if (arg.itemID <= 0 && arg.name == nullptr)
        return 0;

    Item::Location::ByGUIDResult found;
    if (!Item::Location::FindByArg(L, arg, &found))
        return 0;

    // equipmentSlotIndex != 0 → equipped (paperdoll slot); else a bag slot.
    if (found.equipmentSlotIndex != 0)
        Item::Cursor::PickupEquipmentSlot(found.equipmentSlotIndex);
    else
        Item::Cursor::PickupBagItem(L, found.bagID, found.slotIndex);
    return 0;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kPickupItemArgs[] = {
    Game::Doc::Req("itemInfo", "ItemInfo",
                   "An item ID, an item link, or the name of an item you wear or carry."),
};
const Game::Doc::Function kPickupItem{
    "Puts the first matching item from your equipment or bags on the cursor; does nothing when "
    "the cursor holds something or the item is locked.",
    kPickupItemArgs, {}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "PickupItem",
                                     &Script_C_Item_PickupItem, &kPickupItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::Pickup
