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
//
// TryOn is DressUpModel::TryOnItem's arms (0x00504540, benilla ui/dressup)
// for a holder that is not a pane, the ModelScene actor: the pane runs the
// engine's worker itself (model/DressUp.cpp). Two differences:
//   - An evicted lane is zeroed, as Undress zeroes one. The worker leaves its
//     record, which only makes a later try-on detach it again.
//   - A tabard keeps no guild emblem. The worker's emblem step reads the
//     pane's player.

#include "model/Outfit.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/ItemDisplay.h"
#include "model/M2.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

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
using CharCompSetItem_t = void(__thiscall *)(void *component, uint32_t slot, uint32_t displayID);
using CanHoldBoth_t = bool(__fastcall *)(uint32_t mainType, uint32_t offType, bool dualWield);

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

// TryOnItem's hand for a held type: the named one, else a second one-hander
// in the off hand of a dual wielder, and each type's own hand.
int HandFor(uint32_t type, const Lanes &lanes, const HeldTryOn &held) {
    if (held.bodySlot != 0)
        return LaneIndex(held.bodySlot);
    switch (type) {
    case Offsets::INVTYPE_WEAPON:
        return lanes[0].itemClass != 0 &&
                       (lanes[0].inventoryType == Offsets::INVTYPE_WEAPON ||
                        lanes[0].inventoryType == Offsets::INVTYPE_WEAPONMAINHAND) &&
                       held.dualWield
                   ? 1
                   : 0;
    case Offsets::INVTYPE_SHIELD:
    case Offsets::INVTYPE_RANGED:
    case Offsets::INVTYPE_WEAPONOFFHAND:
    case Offsets::INVTYPE_HOLDABLE:
        return 1;
    default: // two-hand, main-hand, thrown and right-hand ranged
        return 0;
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

uint32_t EnchantVisual(const void *display, uint32_t enchantID) {
    if (display == nullptr ||
        Model::ItemDisplay::OwnVisual(static_cast<const uint8_t *>(display)) != 0)
        return 0;
    const uint8_t *enchant = DBC::Record(Offsets::VAR_SPELLITEMENCHANT_RECORDS,
                                         Offsets::VAR_SPELLITEMENCHANT_COUNT, enchantID);
    return enchant != nullptr
               ? Game::Read<uint32_t>(enchant, Offsets::OFF_SPELLITEMENCHANT_ITEM_VISUAL)
               : 0;
}

Arm ArmOf(uint32_t inventoryType, uint32_t *componentSlot) {
    uint32_t slot = 0;
    switch (inventoryType) {
    case Offsets::INVTYPE_HEAD: slot = 0; break;
    case Offsets::INVTYPE_SHOULDER: slot = 1; break;
    case Offsets::INVTYPE_BODY: slot = 2; break;
    case Offsets::INVTYPE_CHEST:
    case Offsets::INVTYPE_ROBE: slot = 3; break;
    case Offsets::INVTYPE_WAIST: slot = 4; break;
    case Offsets::INVTYPE_LEGS: slot = 5; break;
    case Offsets::INVTYPE_FEET: slot = 6; break;
    case Offsets::INVTYPE_WRIST: slot = 7; break;
    case Offsets::INVTYPE_HAND: slot = 8; break;
    case Offsets::INVTYPE_TABARD: slot = 9; break;
    case Offsets::INVTYPE_CLOAK: slot = 10; break;
    case Offsets::INVTYPE_WEAPON:
    case Offsets::INVTYPE_SHIELD:
    case Offsets::INVTYPE_RANGED:
    case Offsets::INVTYPE_2HWEAPON:
    case Offsets::INVTYPE_WEAPONMAINHAND:
    case Offsets::INVTYPE_WEAPONOFFHAND:
    case Offsets::INVTYPE_HOLDABLE:
    case Offsets::INVTYPE_THROWN:
    case Offsets::INVTYPE_RANGEDRIGHT:
        return Arm::Held;
    case Offsets::INVTYPE_NECK:
    case Offsets::INVTYPE_FINGER:
    case Offsets::INVTYPE_TRINKET:
    case Offsets::INVTYPE_RELIC:
        return Arm::Nothing;
    default:
        return Arm::NotEquippable;
    }
    if (componentSlot != nullptr)
        *componentSlot = slot;
    return Arm::Armor;
}

bool CanDualWield(uint64_t guid) {
    return guid != 0 && guid == Unit::Identity::PlayerGuid() &&
           reinterpret_cast<int(__cdecl *)()>(Offsets::FUN_PLAYER_CAN_DUAL_WIELD)() != 0;
}

bool TryOn(void *model, void *component, bool armorReady, Lanes &lanes, Weapons &weapons,
           const uint8_t *record, const HeldTryOn &held) {
    const auto displayID = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    if (model == nullptr || displayID == 0)
        return false;
    const auto type = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    uint32_t componentSlot = 0;
    switch (ArmOf(type, &componentSlot)) {
    case Arm::Armor:
        if (component == nullptr || !armorReady)
            return false;
        reinterpret_cast<UnequipSlot_t>(Offsets::FUN_CHARCOMP_UNEQUIP_SLOT)(component,
                                                                           componentSlot);
        reinterpret_cast<CharCompSetItem_t>(Offsets::FUN_CHARCOMP_SET_ITEM)(component,
                                                                           componentSlot, displayID);
        return true;
    case Arm::Held:
        break;
    default:
        return false;
    }
    const int lane = HandFor(type, lanes, held);
    const int other = 1 - lane;
    if (lanes[other].itemClass != 0) {
        const uint32_t mainType = lane == 0 ? type : lanes[0].inventoryType;
        const uint32_t offType = lane == 0 ? lanes[1].inventoryType : type;
        if (!reinterpret_cast<CanHoldBoth_t>(Offsets::FUN_DRESSUPMODEL_CAN_HOLD_BOTH)(
                mainType, offType, held.dualWield)) {
            ClearLane(model, lanes, other);
            weapons.display[other] = nullptr;
            weapons.visual[other] = 0;
        }
    }
    ClearLane(model, lanes, lane);
    HeldLane &target = lanes[lane];
    target.itemClass = static_cast<uint8_t>(Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_CLASS));
    target.unknown1[0] = static_cast<uint8_t>(Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SUBCLASS));
    target.inventoryType = static_cast<uint8_t>(type);
    target.sheatheType = static_cast<uint8_t>(Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SHEATH));
    const uint8_t *display = DBC::Record(Offsets::VAR_ITEMDISPLAYINFO_RECORDS,
                                         Offsets::VAR_ITEMDISPLAYINFO_COUNT, displayID);
    weapons.display[lane] = display;
    weapons.visual[lane] = EnchantVisual(display, held.enchant);
    if (display == nullptr || held.hide)
        return true;
    const int attachId = reinterpret_cast<AttachItemModel_t>(Offsets::FUN_ATTACH_ITEM_MODEL)(
        model, display, kBodySlots[lane], target.sheatheType, held.sheathed,
        type == Offsets::INVTYPE_SHIELD, type == Offsets::INVTYPE_RANGEDRIGHT);
    HangVisual(model, attachId, weapons.visual[lane]);
    return true;
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
