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

#include "container/Bag.h"

#include "Game.h"
#include "Offsets.h"
#include "equipmentset/Locations.h"
#include "item/BagFamily.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "unit/Identity.h"

namespace Container::Bag {

namespace {

bool PlayerRange(int first, int count, View *out) {
    auto *inv = Unit::Identity::PlayerInventoryManager();
    if (inv == nullptr)
        return false;
    *out = {inv, first, count, 0};
    return true;
}

} // namespace

// The main bank resolves only while a banker is open — 3.3.5 gates it on
// `CGBankInfo::m_banker` the same way. Its GUIDs sit in the player's list
// from login, but the server refuses every bank move without a banker and
// the engine's own slot reader (`0x6228a0`) hides slots 39..68 on the same
// banker-GUID test. Bank bags get that gate from `BagContainer`, whose
// engine GUID getter answers 0 for them while the bank is closed.
bool ViewFor(int bagID, View *out) {
    if (bagID == Offsets::BAG_INDEX_BACKPACK)
        return PlayerRange(Offsets::INVMGR_BACKPACK_FIRST_SLOT,
                           Offsets::BACKPACK_NUM_SLOTS, out);
    if (bagID == Offsets::BAG_INDEX_BANK) {
        if (!EquipmentSet::Locations::BankerOpen())
            return false;
        return PlayerRange(Offsets::INVMGR_BANK_MAIN_FIRST_SLOT,
                           Offsets::INVMGR_BANK_MAIN_LAST_SLOT -
                               Offsets::INVMGR_BANK_MAIN_FIRST_SLOT + 1,
                           out);
    }

    const uint8_t *bag = Item::Location::BagContainer(bagID);
    auto *inv = static_cast<const uint8_t *>(Item::Location::ContainerInventory(bag));
    if (inv == nullptr)
        return false;
    const int slots = static_cast<int>(
        Game::Read<uint32_t>(inv, Offsets::OFF_INVMGR_SLOT_COUNT));
    if (slots <= 0)
        return false;

    // The bag's own family, as 3.3.5's `CGItem_C::GetBagFamily` reads it (0
    // while its record isn't cached). `BitmaskForRecord` supplies the family
    // a vanilla bag's empty field leaves out, so a quiver reports one rather
    // than reading as general-purpose, and agrees with `C_Item.GetItemFamily`.
    uint32_t family = 0;
    const int bagItemID = Item::ID::FromCGItem(bag);
    if (bagItemID > 0) {
        if (auto *rec = Item::PeekRecord(static_cast<uint32_t>(bagItemID)))
            family = Item::BagFamily::BitmaskForRecord(rec);
    }
    *out = {inv, 0, slots, family};
    return true;
}

uint64_t SlotGuid(const View &view, int slotIndex0) {
    const uint32_t linear = static_cast<uint32_t>(view.firstLinear + slotIndex0);
    if (linear >= Game::Read<uint32_t>(view.invMgr, Offsets::OFF_INVMGR_SLOT_COUNT))
        return 0;
    auto *guids = Game::Read<const uint64_t *>(view.invMgr, Offsets::OFF_INVMGR_GUID_ARRAY);
    return guids != nullptr ? guids[linear] : 0;
}

int CountFree(const View &view) {
    if (view.invMgr != Unit::Identity::PlayerInventoryManager()) {
        using CountFreeSlots_t = uint32_t(__thiscall *)(const uint8_t *invMgr);
        auto countFreeSlots =
            reinterpret_cast<CountFreeSlots_t>(Offsets::FUN_INVMGR_COUNT_FREE_SLOTS);
        return static_cast<int>(countFreeSlots(view.invMgr));
    }
    int free = 0;
    for (int i = 0; i < view.slotCount; ++i) {
        if (SlotGuid(view, i) == 0)
            free++;
    }
    return free;
}

} // namespace Container::Bag
