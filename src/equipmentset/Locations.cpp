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

#include "Offsets.h"
#include "item/Location.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>

namespace EquipmentSet::Locations {

namespace {

// Linear slot ranges inside the player invMgr's flat GUID array.
// Documented in Offsets.h ("Slot ranges, matching PackBagSlot's
// linearization"); summarized here for quick reference:
//   0..18    paperdoll equipment slots 1..19 (HEAD..TABARD)
//   19..22   player bag SLOTS — bags 1..4 equipped on character
//   23..38   backpack contents (16 slots)
//   39..62   main bank (24 slots)
//   63..68   bank bag SLOTS — bags 5..10 equipped in bank
//   81+      keyring (irrelevant for equipment sets)
//
// Backpack lives inline in this array; the four player bag bags and
// six bank bag bags each have their own invMgr with the same shape
// (slot count at +0x00, GUID array pointer at +0x04). We reach them
// by resolving the bag's CGContainer via `FUN_OBJECT_RESOLVE_BY_GUID`
// and reading the bag's invMgr off `vtable[+0x10]`.
constexpr int LINEAR_BAG_SLOT_FIRST = 19; // bag1 → linear 19
constexpr int LINEAR_BACKPACK_FIRST = 23;
constexpr int LINEAR_BACKPACK_LAST = 38;

uint64_t *InvMgrGuidArray(const uint8_t *invMgr) {
    if (invMgr == nullptr)
        return nullptr;
    return *reinterpret_cast<uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
}

int InvMgrSlotCount(const uint8_t *invMgr) {
    if (invMgr == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(invMgr));
}

uint8_t *ResolveByGUID(int type, uint64_t guid) {
    if (guid == 0)
        return nullptr;
    return static_cast<uint8_t *>(Object::ByGuid(type, guid, "ItemMgr", 0x172));
}

// Walks one bag's contents for a matching GUID. Returns the 1-based
// slot index of the match, or 0 if not found / bag unresolvable.
// Works for both player bags (linear 19..22 of player invMgr) and
// bank bags (linear 63..68) — the only differences are the source of
// the bag's GUID and how the result encodes back into a location.
int WalkBagContents(uint64_t bagGuid, uint64_t targetGuid) {
    auto *bag = ResolveByGUID(Offsets::TYPEMASK_CONTAINER, bagGuid);
    if (bag == nullptr)
        return 0;
    auto *bagInvMgr =
        static_cast<const uint8_t *>(Item::Location::ContainerInventory(bag));
    if (bagInvMgr == nullptr)
        return 0;
    const int count = InvMgrSlotCount(bagInvMgr);
    auto *guids = InvMgrGuidArray(bagInvMgr);
    if (guids == nullptr)
        return 0;
    for (int i = 0; i < count; ++i) {
        if (guids[i] == targetGuid)
            return i + 1;
    }
    return 0;
}

} // namespace

int FindGUID(uint64_t targetGuid) {
    if (targetGuid == GUID_EMPTY || targetGuid == GUID_IGNORED)
        return 0;

    auto *invMgr = Unit::Identity::PlayerInventoryManager();
    auto *guids = InvMgrGuidArray(invMgr);
    if (guids == nullptr)
        return 0;

    // Paperdoll (linear 0..18 → slots 1..19). Most common hit
    // because every set's items are equipped at save time.
    for (int slot = 1; slot <= SLOT_COUNT; ++slot) {
        if (guids[slot - 1] == targetGuid)
            return PackEquipped(slot);
    }

    // Backpack contents (linear 23..38 → bag 0, slots 1..16).
    for (int s = LINEAR_BACKPACK_FIRST; s <= LINEAR_BACKPACK_LAST; ++s) {
        if (guids[s] == targetGuid)
            return PackBag(0, s - LINEAR_BACKPACK_FIRST + 1);
    }

    // Main bank (linear 39..62 → slots 1..24). Direct read; the bank
    // gate at `VAR_BANK_GATE_GUID` doesn't affect the underlying data.
    for (int s = Offsets::INVMGR_BANK_MAIN_FIRST_SLOT;
         s <= Offsets::INVMGR_BANK_MAIN_LAST_SLOT; ++s) {
        if (guids[s] == targetGuid)
            return PackMainBank(s - Offsets::INVMGR_BANK_MAIN_FIRST_SLOT + 1);
    }

    // Player bags 1..4 — bag containers live at linear 19..22 of the
    // player invMgr; their contents live in each bag's own invMgr.
    for (int bag = 1; bag <= 4; ++bag) {
        const int linear = LINEAR_BAG_SLOT_FIRST + (bag - 1);
        const int slot = WalkBagContents(guids[linear], targetGuid);
        if (slot > 0)
            return PackBag(bag, slot);
    }

    // Bank bags 5..10 — bag containers at linear 63..68. Same walk as
    // player bags; works without the bank window being open.
    for (int linear = Offsets::INVMGR_BANK_BAG_FIRST_SLOT;
         linear <= Offsets::INVMGR_BANK_BAG_LAST_SLOT; ++linear) {
        const int slot = WalkBagContents(guids[linear], targetGuid);
        if (slot > 0) {
            const int bagID = 5 + (linear - Offsets::INVMGR_BANK_BAG_FIRST_SLOT);
            return PackBankBag(bagID, slot);
        }
    }

    return 0;
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
    auto *guids = InvMgrGuidArray(invMgr);
    if (guids == nullptr)
        return;
    for (int i = 0; i < SLOT_COUNT; ++i)
        out[i] = guids[i];
}

bool ContainsLockedItems(const Set &set) {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = set.items[i];
        if (g == GUID_EMPTY || g == GUID_IGNORED)
            continue;
        if (FindGUID(g) == 0)
            continue;
        const uint8_t *item = ResolveItemByGUID(g);
        if (item == nullptr)
            continue;
        const uint32_t flags = *reinterpret_cast<const uint32_t *>(
            item + Offsets::OFF_ITEM_CLIENT_LOCK);
        if (flags & Offsets::ITEM_CLIENT_LOCK_BIT)
            return true;
    }
    return false;
}

} // namespace EquipmentSet::Locations
