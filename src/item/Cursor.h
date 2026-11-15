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

#pragma once

// Cursor pickup primitives — thin C++ wrappers over the engine's own
// pickup functions (`FUN_PICKUP_INVENTORY_SLOT` / `FUN_CURSOR_PICKUP_ITEM`),
// called directly rather than through the Lua `Script_Pickup*` state-machine
// wrappers (which would require faking the Lua stack). Shared by
// `C_Item.PickupItem` and `C_Item.EquipItemByName`'s auto-slot path.
namespace Item::Cursor {

// Picks the item in a 1-based character-pane equipment slot onto the
// cursor via the engine's self-contained equipment-pickup primitive
// (which resolves the player and applies its own cursor/lock guards).
void PickupEquipmentSlot(int equipmentSlotIndex);

// Picks the item in a bag slot onto the cursor. Guarded to match the Lua
// wrapper's behavior: no-op (returns false) when the slot is empty, the
// cursor already holds an item, or the item is locked (mid-transaction).
// Returns true when the pickup was issued.
bool PickupBagItem(int bagID, int slotIndex);

} // namespace Item::Cursor
