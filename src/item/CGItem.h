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

#include <cstdint>

// Accessors for a `CGItem`'s descriptor fields. Its GUID and itemID sit in the
// object data block every object has (Object::Guid, Item::ID::FromCGItem).

namespace Item {

// The item's **m_objectFields descriptor** (`CGItem + 0x114` dereferenced):
// the UpdateField block holding stack count (`+0x20`), flags (`+0x3C`),
// spell charges (`+0x28`), durability (`+0xA0`), enchantments, etc. Distinct
// from the instance block. Returns nullptr for a null `cgItem`.
const uint8_t *ObjectFields(const uint8_t *cgItem);

// ITEM_FIELD_STACK_COUNT; 0 for a null `cgItem` or a missing descriptor.
uint32_t StackCount(const uint8_t *cgItem);

// ITEM_FIELD_FLAGS (`ITEM_FLAG_*`); 0 for a null `cgItem` or a missing
// descriptor.
uint32_t Flags(const uint8_t *cgItem);

// The client-side "in-transaction" lock (`OFF_ITEM_CLIENT_LOCK` bit 0): set
// while the item is on the cursor or attached to a mail or trade, until the
// server confirms. False for a null `cgItem`.
bool IsLocked(const uint8_t *cgItem);

// Sets or clears that lock through the engine's own setter / clearer
// (`FUN_ITEM_LOCK_BY_GUID` / `FUN_ITEM_UNLOCK_BY_GUID`), which resolve the
// item by GUID and fire ITEM_LOCK_CHANGED. A GUID that names no item still
// fires the event and changes nothing.
void SetLocked(uint64_t guid, bool locked);

} // namespace Item
