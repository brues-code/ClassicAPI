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

#include "AutoList.h"

#include <cstdint>

// Shared BagFamily derivation for a cached `ItemStats_C` record. Used by
// `C_Item.GetItemFamily` and `C_Container.GetContainerNumFreeSlots` /
// `CalculateTotalNumberOfFreeBagSlots` so the family a bag reports matches
// the family the items it holds report.
//
// This module knows only STOCK vanilla data. Server-custom container
// families (e.g. Turtle's cooking/gathering bags) plug in through the
// `AutoResolver` extension point below, so non-stock hardcoded data lives
// in its own module (see `src/turtle/`) rather than here.
namespace Item::BagFamily {

// Item.dbc `m_class` values. Containers hold the profession/soul bags;
// Quivers hold arrows (subclass 2) and bullets/ammo pouches (subclass 3).
// See ItemClass.dbc.
constexpr uint32_t kItemClassContainer = 1;
constexpr uint32_t kItemClassQuiver = 11;

// Convert 1.12's raw 1-based `ItemBagFamily.dbc` ID to the modern bitmask
// encoding (`1 << (id - 1)`); `0` stays `0`. Vanilla stores the raw ID
// (`arrow=1, bullet=2, soul shard=3, herb=6, …`); Wrath+ flipped the same
// field to the bitmask form, which is what backported addons expect.
inline uint32_t IdToBitmask(uint32_t rawId) {
    return (rawId == 0) ? 0 : (1u << (rawId - 1));
}

// (class, subclass) → `ItemBagFamily.dbc` raw ID, for bags/quivers whose
// `m_bagFamily` field shipped empty. Covers the stock vanilla profession
// bags (Container class: Soul/Herb/Enchanting/Engineering) and quivers
// (Quiver class: Quiver→arrows, Ammo Pouch→bullets); for anything else it
// consults the registered custom resolvers (see `AutoResolver`) before
// giving up with `0`. Keyed on class because subclass numbers repeat
// across classes (Container subclass 2 = Herb Bag, Quiver subclass 2 =
// Quiver). Returns a raw family ID, not a bitmask.
uint32_t SubclassToId(uint32_t itemClass, uint32_t subclass);

// BagFamily bitmask for a cached `ItemStats_C` record: the item's
// `m_bagFamily` field if populated, otherwise — for a container whose
// field is empty — derived from the container subclass. Pass a non-null
// record.
uint32_t BitmaskForRecord(const uint8_t *record);

// Extension point for server-custom bag families. A resolver maps a
// (class, subclass) pair to an `ItemBagFamily.dbc` raw ID, or returns `0`
// if it doesn't recognize it. Consulted by `SubclassToId` only after the
// stock table misses. Declare a file-scope
// `static const Item::BagFamily::AutoResolver` in a platform module to
// register one at static-init time.
using Resolver = uint32_t (*)(uint32_t itemClass, uint32_t subclass);
using AutoResolver = AutoList<struct AutoResolverTag, Resolver>;

} // namespace Item::BagFamily
