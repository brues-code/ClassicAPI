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

// Retail's Enum.TransmogOutfitSlotOptionSheatheCategory: where a held item
// hangs while sheathed.
enum SheatheCategory : uint32_t {
    kSheatheDefault = 0, // the item's own sheath point
    kSheatheBack = 1,
    kSheatheSide = 2,
    kSheatheHide = 3, // no sheath point: nothing shows while sheathed
};

// The sheathe type an item whose own type is `own` (OFF_ITEMSTATS_SHEATH)
// hangs with in `category`: Back moves a hip weapon to the back pair, Side
// moves a back weapon to the hip, Hide leaves no sheath point. Every other
// item keeps its own (a shield, an item with no sheath point).
uint8_t SheatheTypeFor(uint8_t own, uint32_t category);

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

// Lane `i` hung again with `sheatheType`: detached at the type it hangs with
// now (DetachHeldItemSlot finds the sheath point from it), then hung in the
// hand or the sheath, or left off. The lane keeps the new type, so every
// later detach finds the new sheath point.
void SetSheatheType(void *model, Lanes &lanes, int i, const Weapons &weapons,
                    uint8_t sheatheType, bool sheathed, bool hide);

// The glow `enchantID` gives an item shown with `display` (an ItemDisplayInfo
// row): none when the display hangs its own (GetItemDescVisual's rule).
uint32_t EnchantVisual(const void *display, uint32_t enchantID);

// TryOnItem's dispatch (0x00504540) on an item's inventory type.
enum class Arm {
    Armor,         // a component slot
    Held,          // a held-item lane
    Nothing,       // neck, finger, trinket, relic: nothing, and no error
    NotEquippable, // anything else: the worker shows ERR_NOT_EQUIPPABLE
};

// The arm for `inventoryType`; `componentSlot` receives an armor arm's slot.
Arm ArmOf(uint32_t inventoryType, uint32_t *componentSlot = nullptr);

// DressUpModel's dual-wield test: the model shows the active player, and the
// player can dual wield.
bool CanDualWield(uint64_t guid);

// How a held try-on is hung.
struct HeldTryOn {
    uint32_t bodySlot = 0; // the hand named (kBodySlot*), or 0 for the worker's choice
    bool dualWield = false;
    bool sheathed = false;
    bool hide = false;
    uint32_t enchant = 0; // its glow's SpellItemEnchantment, or 0
};

// TryOnItem's arms on whatever holds the component and the lanes: an armor
// slot is cleared and set on the component, which `armorReady` (the pane's
// ready byte) must allow; a held item evicts an other-lane item it cannot be
// held with, then takes its lane and hangs. The inventory slot (1-based) the
// item went on, or 0.
int TryOn(void *model, void *component, bool armorReady, Lanes &lanes, Weapons &weapons,
          const uint8_t *record, const HeldTryOn &held);

// UndressSlot: the item in one inventory slot (1-based) off the model. A
// held lane is detached and zeroed, an armor slot cleared on the component.
// Without a component it does nothing, held lanes included (4.3.4's
// CGDressUpModelFrame::UndressSlot returns first).
void UndressSlot(void *model, void *component, Lanes &lanes, int invSlot);

// The component slot of an inventory slot (1-based), or -1 for a slot the
// component has no record for (the neck, fingers, trinkets, hands held and
// ranged).
int ComponentSlotOf(int invSlot);

// The inventory slot (1-based) of a component slot ArmOf gives, or 0.
int InvSlotOfComponent(uint32_t componentSlot);

// Whether TryOnItem's arms put anything in an inventory slot (1-based): a
// hand takes any held item, an armor slot needs `armorReady` (a component the
// model's ready gate allows).
bool SlotAllowed(int invSlot, bool armorReady);

// What `unit` wears in one inventory slot (1-based), back on the model: an
// armor slot takes the unit's component record (empty when the unit hides
// its helm or cloak), a held lane the unit's lane, row and glow. Armor needs
// a component and `armorReady`.
void DressSlot(void *model, void *component, bool armorReady, Lanes &lanes, Weapons &weapons,
               void *unit, uint64_t guid, int invSlot, bool sheathed, bool hide);

// DressUpModel ClearModel (0x00504490): component slots 0..11 cleared, and,
// with `weapons`, both lanes detached and zeroed.
void Undress(void *model, void *component, Lanes &lanes, bool weapons);

// What one inventory slot wears, as retail's ItemTransmogInfo names it: the
// item (1.12's appearance) and, on a hand, the SpellItemEnchantment whose
// glow it shows. Zero for nothing.
struct Appearance {
    uint32_t item = 0;
    uint32_t enchant = 0;
};
constexpr int kInventorySlots = 19;
using Worn = Appearance[kInventorySlots]; // inventory slot n at [n - 1]

// What the unit `guid` wears where its model shows it: each armor slot its
// component shows and each hand `weapons` (ReadWeapons) holds. Nothing for a
// unit that is not a player, which has no visible items.
void ReadWorn(void *unit, uint64_t guid, const Weapons &weapons, Worn &worn);

// Zeroes each slot of `worn` the model no longer shows: an armor slot the
// component holds no record for (every one without a component), an empty
// lane.
void TrimWorn(void *component, const Lanes &lanes, Worn &worn);

} // namespace Model::Outfit
