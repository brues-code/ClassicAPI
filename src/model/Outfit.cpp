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

// The outfit calls of a character model copy: DressUpModel's own steps
// (SetModelInstance 0x00504350, ClearModel 0x00504490, 4.3.4's per-slot
// UndressSlot) and the unit's AttachHandItem step, on whatever holds the
// component and the lanes. benilla ui/dressup and charactermodel/equipment.
//
// The slot map is 4.3.4's UndressSlot map, from inventory slot to component
// slot.

#include "model/Outfit.h"

#include "Game.h"
#include "Offsets.h"
#include "model/M2.h"
#include "object/Resolve.h"

namespace Model::Outfit {
namespace {

using UnequipSlot_t = void(__thiscall *)(void *component, uint32_t slot);
using DetachHeldItem_t = void(__fastcall *)(void *model, uint32_t bodySlot, uint32_t sheatheType,
                                            bool shield);
using AttachItemModel_t = int(__fastcall *)(void *model, const void *record, uint32_t bodySlot,
                                            uint32_t sheatheType, bool sheathed, bool shield,
                                            bool ranged);
using GetHeldItemDisplay_t = const void *(__thiscall *)(void *unit, uint32_t hand);
using GetHeldItemLane_t = const HeldLane *(__thiscall *)(void *unit, uint32_t hand, int one);
using GetVisibleItem_t = const void *(__thiscall *)(void *player, uint32_t slot);
using GetDescVisual_t = int(__thiscall *)(const void *visibleItem);
using CharCompCreate_t = void *(__cdecl *)();
using CharCompClone_t = bool(__thiscall *)(void *component, void *model, void *source);

constexpr uint32_t kBodySlots[2] = {kBodySlotMainHand, kBodySlotOffHand};

// The component slots ClearModel clears (0..11).
constexpr uint32_t kComponentSlots = 12;

template <typename Fn>
Fn VirtualOf(void *object, uintptr_t slot) {
    return Game::Read<Fn>(Game::Read<const void *>(object, 0), slot);
}

// DetachHeldItemSlot on lane `i`'s item.
void DetachLane(void *model, int i, const HeldLane &lane) {
    reinterpret_cast<DetachHeldItem_t>(Offsets::FUN_DETACH_HELD_ITEM_SLOT)(
        model, kBodySlots[i], lane.sheatheType, lane.inventoryType == Offsets::INVTYPE_SHIELD);
}

// ClearModel's lane step: detach the held item and zero the lane.
void ClearLane(void *model, Lanes &lanes, int i) {
    if (lanes[i].itemClass == 0)
        return;
    DetachLane(model, i, lanes[i]);
    lanes[i] = HeldLane{};
}

// Component slot for a 0-based inventory slot, or -1.
int ComponentSlot(int slot0) {
    switch (slot0) {
    case 0: return 0;   // head
    case 2: return 1;   // shoulder
    case 3: return 2;   // shirt
    case 4: return 3;   // chest
    case 5: return 4;   // waist
    case 6: return 5;   // legs
    case 7: return 6;   // feet
    case 8: return 7;   // wrist
    case 9: return 8;   // hands
    case 14: return 10; // back
    case 18: return 9;  // tabard
    default: return -1;
    }
}

} // namespace

void ReadLanes(void *unit, Lanes &lanes) {
    const auto get = VirtualOf<GetHeldItemLane_t>(unit, Offsets::VTBL_UNIT_GET_HELD_ITEM_LANE);
    for (uint32_t hand = 0; hand < 2; ++hand) {
        const HeldLane *lane = get(unit, hand, 1);
        lanes[hand] = lane != nullptr ? *lane : HeldLane{};
    }
}

void ReadWeapons(uint64_t guid, Weapons &weapons) {
    weapons = Weapons{};
    if (void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid)) {
        const auto get =
            VirtualOf<GetHeldItemDisplay_t>(unit, Offsets::VTBL_UNIT_GET_HELD_ITEM_DISPLAY);
        weapons.display[0] = get(unit, 0);
        weapons.display[1] = get(unit, 1);
    }
    // AttachHandEnchant's glow (0x005EED00), for players only: the visible
    // item in the lane's slot (0xF / 0x10, the same numbers as the body
    // slots), and its visual unless the display hangs its own (-1).
    void *player = Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid);
    if (player == nullptr)
        return;
    for (int i = 0; i < 2; ++i) {
        const void *item =
            reinterpret_cast<GetVisibleItem_t>(Offsets::FUN_UNIT_GET_VISIBLE_ITEM)(player,
                                                                                kBodySlots[i]);
        if (item == nullptr || Game::Read<uint32_t>(item, Offsets::OFF_VISIBLE_ITEM_ITEM_ID) == 0)
            continue;
        const int visual =
            reinterpret_cast<GetDescVisual_t>(Offsets::FUN_VISIBLE_ITEM_GET_DESC_VISUAL)(item);
        weapons.visual[i] = visual > 0 ? static_cast<uint32_t>(visual) : 0;
    }
}

void *CloneComponent(void *model, void *unit) {
    void *source = Game::Read<void *>(unit, Offsets::OFF_CGUNIT_CHARACTER_COMPONENT);
    if (source == nullptr)
        return nullptr;
    void *component = reinterpret_cast<CharCompCreate_t>(Offsets::FUN_CHARCOMP_CREATE)();
    if (component == nullptr)
        return nullptr;
    M2::AddRef(model); // the component's reference
    reinterpret_cast<CharCompClone_t>(Offsets::FUN_CHARCOMP_CLONE)(component, model, source);
    return component;
}

void HangVisual(void *model, int attachId, uint32_t visual) {
    if (visual == 0 || attachId < 0)
        return;
    if (void *item = M2::FindChild(model, attachId))
        M2::AttachItemVisuals(item, visual);
}

void HangWeapons(void *model, const Lanes &lanes, const Weapons &weapons, bool sheathed,
                 bool hide) {
    if (model == nullptr)
        return;
    for (int i = 0; i < 2; ++i) {
        const HeldLane &lane = lanes[i];
        if (lane.itemClass == 0 || weapons.display[i] == nullptr)
            continue;
        DetachLane(model, i, lane);
        if (hide)
            continue;
        const int attachId = reinterpret_cast<AttachItemModel_t>(Offsets::FUN_ATTACH_ITEM_MODEL)(
            model, weapons.display[i], kBodySlots[i], lane.sheatheType, sheathed,
            lane.inventoryType == Offsets::INVTYPE_SHIELD,
            lane.inventoryType == Offsets::INVTYPE_RANGEDRIGHT);
        HangVisual(model, attachId, weapons.visual[i]);
    }
}

void UndressSlot(void *model, void *component, Lanes &lanes, int invSlot) {
    if (model == nullptr || component == nullptr)
        return;
    if (invSlot == Offsets::INVSLOT_MAINHAND) {
        ClearLane(model, lanes, 0);
    } else if (invSlot == Offsets::INVSLOT_OFFHAND) {
        ClearLane(model, lanes, 1);
    } else {
        const int compSlot = ComponentSlot(invSlot - 1);
        if (compSlot >= 0)
            reinterpret_cast<UnequipSlot_t>(Offsets::FUN_CHARCOMP_UNEQUIP_SLOT)(
                component, static_cast<uint32_t>(compSlot));
    }
}

void Undress(void *model, void *component, Lanes &lanes, bool weapons) {
    if (model == nullptr)
        return;
    if (component != nullptr) {
        for (uint32_t slot = 0; slot < kComponentSlots; ++slot)
            reinterpret_cast<UnequipSlot_t>(Offsets::FUN_CHARCOMP_UNEQUIP_SLOT)(component, slot);
    }
    if (weapons) {
        ClearLane(model, lanes, 0);
        ClearLane(model, lanes, 1);
    }
}

} // namespace Model::Outfit
