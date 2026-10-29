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

// Accessors for a `CGItem`'s two descriptor-like pointers (see the "Item
// lookups" notes in CLAUDE.md). Every item module used to spell out the same
// `*reinterpret_cast<const uint8_t *const *>(cgItem + OFF_...)` deref; these
// name it once.

namespace Item {

// The item's **instance block** (`CGItem + 0x08` dereferenced): a `uint64`
// item GUID at `+0x00` and the `uint32` itemID at `+0x0C`. This is the block
// the canonical inventory→cache path reads the itemID from. Returns nullptr
// for a null `cgItem`; the stored pointer itself may still be null (callers
// null-check the result before reading fields).
const uint8_t *InstanceBlock(const uint8_t *cgItem);

// The item's GUID, read from its instance block. 0 for a null `cgItem` or
// a missing block.
uint64_t Guid(const uint8_t *cgItem);

// The item's **m_objectFields descriptor** (`CGItem + 0x114` dereferenced):
// the UpdateField block holding stack count (`+0x20`), flags (`+0x3C`),
// spell charges (`+0x28`), durability (`+0xA0`), enchantments, etc. Distinct
// from the instance block. Returns nullptr for a null `cgItem`.
const uint8_t *ObjectFields(const uint8_t *cgItem);

} // namespace Item
