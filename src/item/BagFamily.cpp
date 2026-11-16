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

#include "item/BagFamily.h"

#include "Offsets.h"

namespace Item::BagFamily {

namespace {

// Chained at static-init by each `AutoResolver` constructor, before
// `DllMain`. Zero-initialized (namespace static) ahead of any dynamic
// init, so the head is valid when resolvers link in.

// Stock vanilla bags/quivers that shipped with an empty `m_bagFamily`
// field, recovered from (class, subclass). Correspondence cross-referenced
// by name between ItemSubClass.dbc and ItemBagFamily.dbc. Everything else
// in stock vanilla either carries the field directly or has no family, so
// it isn't listed — server-custom bags come in through the resolver chain.
uint32_t StockSubclassToId(uint32_t itemClass, uint32_t subclass) {
    if (itemClass == kItemClassContainer) {
        switch (subclass) {
        case 1:  return 3;   // Soul Bag        → Soul Shards
        case 2:  return 6;   // Herb Bag        → Herbs
        case 3:  return 7;   // Enchanting Bag  → Enchanting Supplies
        case 4:  return 8;   // Engineering Bag → Engineering Supplies
        default: return 0;
        }
    }
    if (itemClass == kItemClassQuiver) {
        switch (subclass) {
        case 2:  return 1;   // Quiver     → Arrows
        case 3:  return 2;   // Ammo Pouch → Bullets
        default: return 0;
        }
    }
    return 0;
}

} // namespace

uint32_t SubclassToId(uint32_t itemClass, uint32_t subclass) {
    if (const uint32_t id = StockSubclassToId(itemClass, subclass))
        return id;
    for (AutoResolver *r = AutoResolver::head; r != nullptr; r = r->next)
        if (const uint32_t id = r->fn(itemClass, subclass))
            return id;
    return 0;
}

uint32_t BitmaskForRecord(const uint8_t *record) {
    uint32_t rawId = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_BAG_FAMILY);
    if (rawId == 0) {
        const uint32_t cls = *reinterpret_cast<const uint32_t *>(
            record + Offsets::OFF_ITEMSTATS_CLASS);
        const uint32_t sub = *reinterpret_cast<const uint32_t *>(
            record + Offsets::OFF_ITEMSTATS_SUBCLASS);
        rawId = SubclassToId(cls, sub);
    }
    return IdToBitmask(rawId);
}

} // namespace Item::BagFamily
