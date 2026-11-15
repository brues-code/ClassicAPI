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

#include "Locations.h"

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "unit/Identity.h"

#include <cstdint>

namespace EquipmentSet::Locations {

int FindGUID(uint64_t targetGuid, bool includeBank) {
    if (targetGuid == GUID_EMPTY || targetGuid == GUID_IGNORED)
        return 0;
    Item::Location::ByGUIDResult found;
    if (!Item::Location::FindByGUID(targetGuid, includeBank, &found))
        return 0;
    // A set item counts where it can be equipped from: worn (gear slots
    // 1..19), carried in the backpack or a bag, or banked (main bank or a
    // bank bag). Equipped bags, bank bag slots, the keyring and buyback
    // hold nothing a set names.
    switch (found.place) {
    case Item::Location::PLACE_EQUIPPED:
        return found.equipmentSlotIndex <= SLOT_COUNT ? PackEquipped(found.equipmentSlotIndex)
                                                      : 0;
    case Item::Location::PLACE_BAGS:
        return PackBag(found.bagID, found.slotIndex);
    case Item::Location::PLACE_BANK:
        if (found.equipmentSlotIndex != 0)
            return 0;
        return found.bagID == Offsets::BAG_INDEX_BANK ? PackMainBank(found.slotIndex)
                                                      : PackBankBag(found.bagID, found.slotIndex);
    default:
        return 0;
    }
}

const uint8_t *ResolveItemByGUID(uint64_t guid) {
    if (guid == GUID_EMPTY || guid == GUID_IGNORED)
        return nullptr;
    return Item::Location::ResolveByGUID(guid);
}

void SnapshotPaperdoll(uint64_t *out) {
    for (int i = 0; i < SLOT_COUNT; ++i)
        out[i] = 0;
    auto *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr == nullptr)
        return;
    auto *guids = Game::Read<const uint64_t *>(invMgr, Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guids == nullptr)
        return;
    for (int i = 0; i < SLOT_COUNT; ++i)
        out[i] = guids[i];
}

Status GetStatus(const Set &set) {
    Status st{};
    const bool bank = Item::Location::BankerOpen();
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = set.items[i];
        if (g == GUID_EMPTY)
            continue;
        if (g == GUID_IGNORED) {
            ++st.ignored;
            continue;
        }
        const int loc = FindGUID(g, bank);
        if (loc == 0)
            st.missingSlots[st.missing++] = i + 1;
        else if ((loc & (LOC_BAGS | LOC_BANK)) == 0)
            ++st.equipped;
        else
            ++st.inInventory;
    }
    st.total = st.equipped + st.inInventory + st.missing;
    return st;
}

bool IsEquipped(const Set &set) {
    uint64_t worn[SLOT_COUNT];
    SnapshotPaperdoll(worn);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (set.items[i] != GUID_IGNORED && set.items[i] != worn[i])
            return false;
    }
    return true;
}

namespace {

bool IsLocked(uint64_t guid) {
    return Item::IsLocked(ResolveItemByGUID(guid));
}

} // namespace

bool ContainsLockedItems(const Set &set) {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = set.items[i];
        if (g == GUID_EMPTY || g == GUID_IGNORED)
            continue;
        if (FindGUID(g, true) != 0 && IsLocked(g))
            return true;
    }
    return false;
}

bool UsesLockedSlots(const Set &set) {
    uint64_t worn[SLOT_COUNT];
    SnapshotPaperdoll(worn);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (set.items[i] != GUID_IGNORED && worn[i] != 0 && IsLocked(worn[i]))
            return true;
    }
    return false;
}

} // namespace EquipmentSet::Locations
