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

// What a copy of a unit's character model wears, apart from its own model:
// the character component that composites its armor, and the two held-item
// lanes. 1.12's DressUpModel keeps these in the pane (benilla ui/dressup);
// the ModelScene actor keeps its own. Both change them with the calls here.
namespace Model::Outfit {

// Body slots of the two held lanes.
constexpr uint32_t kBodySlotMainHand = 0xF;
constexpr uint32_t kBodySlotOffHand = 0x10;

// A held lane, as the unit's VTBL_UNIT_GET_HELD_ITEM_LANE returns it and the
// pane stores it (OFF_DRESSUPMODEL_*_HAND_LANE).
struct HeldLane {
    uint8_t itemClass; // 0 = empty
    uint8_t unknown1[2];
    uint8_t inventoryType;
    uint8_t sheatheType;
    uint8_t unknown5[3];
};
static_assert(sizeof(HeldLane) == 8, "ClearModel zeroes 8 bytes per lane");
using Lanes = HeldLane[2]; // main hand, off hand

// The row and glow each lane's item is hung with. The lanes keep no display
// row, so whoever re-hangs a weapon keeps these beside them.
struct Weapons {
    const void *display[2] = {}; // ItemDisplayInfo row on each lane, or null
    uint32_t visual[2] = {};     // enchant ItemVisuals row on each lane, or 0
};

// The lane (0 or 1) of a held body slot.
inline int LaneIndex(uint32_t bodySlot) { return bodySlot == kBodySlotOffHand ? 1 : 0; }

// DressUpModel SetModelInstance's lane copy (0x00504350): the unit's two
// held-item lanes, zero where a hand is empty.
void ReadLanes(void *unit, Lanes &lanes);

// The rows the unit `guid` hangs its held items with (AttachHandItem's
// source), and, for a player, each one's enchant glow (AttachHandEnchant).
void ReadWeapons(uint64_t guid, Weapons &weapons);

// A new character component for `model`, cloned from the unit's, as
// DressUpModel SetModelInstance clones it; null when the unit has none.
// It is pumped and released like a dresser (Model::DisplayInfo::PumpDresser,
// DestroyDresser): both are the engine's character component.
void *CloneComponent(void *model, void *unit);

// AttachEnchantVisual's step (0x005EED50): the glow on the item hung at
// `attachId`.
void HangVisual(void *model, int attachId, uint32_t visual);

// The unit's AttachHandItem step on each occupied lane: detach it, then hang
// it in the hand or the sheath, or leave it off.
void HangWeapons(void *model, const Lanes &lanes, const Weapons &weapons, bool sheathed,
                 bool hide);

// UndressSlot: the item in one inventory slot (1-based) off the model. A
// held lane is detached and zeroed, an armor slot cleared on the component
// (nothing without one).
void UndressSlot(void *model, void *component, Lanes &lanes, int invSlot);

// DressUpModel ClearModel (0x00504490): component slots 0..11 cleared, and,
// with `weapons`, both lanes detached and zeroed.
void Undress(void *model, void *component, Lanes &lanes, bool weapons);

} // namespace Model::Outfit
