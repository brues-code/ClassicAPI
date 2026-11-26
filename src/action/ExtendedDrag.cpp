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

// PlaceOnActionSlot / PickupActionSlot for Action::Extended's slots, line for
// line from the 1.12 functions (decompiled; benilla-re ui/actionuse), with the
// slot word read from and written to our store instead of VAR_ACTION_TABLE.
// Every cursor change goes through the engine's own setters, so the drag
// picture, the grab / drop sounds, ACTIONBAR_SHOWGRID / HIDEGRID and
// CURSOR_UPDATE are the engine's, and a drag between one of our slots and one
// of the engine's 120 works both ways: each side only reads and writes the
// cursor.
//
// Place (0x004E62E0):
//   - FUN_CURSOR_EDIT_ALLOWED(9) or nothing.
//   - Candidates in the engine's order: the cursor spell (VAR_CURSOR_SPELL_ID),
//     the item (an action-bar item's entry for kind 7, else the entry of the
//     kind-1 bag item), the macro (VAR_CURSOR_MACRO_ID). The first one set
//     decides; any other cursor (money, pet action, merchant row, an
//     equipment set) matches none and stays where it is.
//   - Spell: no record → nothing; passive (Attributes & 0x40) → error 158
//     ERR_PASSIVE_ABILITY, cursor kept; the slot's own spell → clear a spell
//     cursor only. The engine's same-spell compare resolves the slot's spell;
//     Action::Place redirects that call to answer only for a spell word, so
//     a spell dropped on a macro replaces it. The compare here is the
//     redirected one.
//   - Item: no active player or no cached template → nothing; neither an
//     on-use spell (id set, trigger 0, in any of five slots) nor an inventory
//     type → nothing; the slot's own item → ClearCursor(1,1) only.
//   - Macro: the slot's own macro → ClearCursor(1,1) only.
//   - Then the old word goes to the cursor (item: kind 7 with its entry, the
//     cached template's display id or 0, and the slot; spell: the spell;
//     live macro: the macro; anything else: ClearCursor(1,1)), the new word
//     is stored, and the slot is announced.
//   The engine also recounts an item word into VAR_ACTION_ITEM_COUNTS; our
//   slots have no count yet (no reader asks for one).
//
// Pickup (0x004E6130): a cursor Place would take → Place. An empty slot →
// nothing. Else the word goes to the cursor as above — a macro that is gone
// goes nowhere — and the slot is cleared.
//
// Equipment sets, as EquipmentSet::Action's co-hooks give them to the
// engine's slots (3.3.5's set arms):
//   - A set on the cursor (Place, Pickup, and UseAction with checkCursor) is
//     dropped by PlaceSet: edit gate, then a gone set or the slot's own set
//     only clears the cursor; otherwise the cursor is cleared, the old word
//     goes to the cursor, and the set word is stored.
//   - A set word goes to the cursor through EquipmentSet::Action::Pickup
//     (nothing when the set is gone), where the engine, which has no set
//     cursor, clears the cursor and the co-hook then picks the set up.

#include "action/ExtendedDrag.h"

#include "Game.h"
#include "Offsets.h"
#include "action/Extended.h"
#include "cursor/Info.h"
#include "equipmentset/Action.h"
#include "equipmentset/Data.h"
#include "item/Record.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Action::ExtendedDrag {

namespace {

using EditAllowed_t = int(__fastcall *)(int kind);
using CursorClear_t = void(__fastcall *)(int returnItem, int fireMoneyEvent);
using CursorClearIfSpell_t = void(__cdecl *)();
using PickupSpell_t = void(__fastcall *)(uint32_t spellID);
using PickupMacro_t = void(__fastcall *)(uint32_t macroID);
using PickupVirtualItem_t = void(__fastcall *)(uint32_t entry, uint32_t displayID, uint32_t index,
                                               uint32_t mode);
using DisplayError_t = void(__cdecl *)(uint32_t id);

constexpr uint32_t kCursorItem = 1;
constexpr uint32_t kCursorActionItem = 7;

void ClearCursor() {
    reinterpret_cast<CursorClear_t>(Offsets::FUN_CURSOR_CLEAR)(1, 1);
}

// GetCursorItemGuid (0x00494C60): the bag item's GUID, only for kind 1.
uint64_t CursorItemGuid() {
    if (Game::Read<uint32_t>(Offsets::VAR_CURSOR_TYPE) != kCursorItem)
        return 0;
    return Game::Read<uint64_t>(Offsets::VAR_CURSOR_ITEM_GUID_LO);
}

// GetCursorAmmo (0x00495170) gated on kind 7, as both callers gate it.
uint32_t CursorActionItem() {
    if (Game::Read<uint32_t>(Offsets::VAR_CURSOR_TYPE) != kCursorActionItem)
        return 0;
    return Game::Read<uint32_t>(Offsets::VAR_CURSOR_GENERIC_SLOT);
}

uint32_t ItemOf(uint32_t word) {
    return (word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_ITEM_BY_ID
               ? word & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID
               : 0;
}

uint32_t MacroOf(uint32_t word) {
    return word != 0 && (word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_BAG_OR_MACRO
               ? word & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO
               : 0;
}

// The template's display id, 0 while it isn't cached — the engine reads it
// with a passive GetRecord at every one of these sites.
uint32_t DisplayOf(uint32_t itemID) {
    const uint8_t *record = Item::PeekRecord(itemID);
    return record != nullptr ? Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID)
                             : 0;
}

// The word's content onto the cursor. False when it has none to give (empty,
// a macro that is gone), where Place clears the cursor and Pickup does not.
bool ToCursor(int slot0, uint32_t word) {
    if (word == 0)
        return false;
    if (const uint32_t itemID = ItemOf(word)) {
        reinterpret_cast<PickupVirtualItem_t>(Offsets::FUN_CURSOR_PICKUP_VIRTUAL_ITEM)(
            itemID, DisplayOf(itemID), static_cast<uint32_t>(slot0), kCursorActionItem);
        return true;
    }
    if ((word & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_SPELL) {
        uint32_t book = 0;
        reinterpret_cast<PickupSpell_t>(Offsets::FUN_CURSOR_PICKUP_SPELL)(
            Extended::SpellFor(word, &book));
        return true;
    }
    if (const uint32_t macroID = Extended::LiveMacroID(word)) {
        reinterpret_cast<PickupMacro_t>(Offsets::FUN_MACRO_PICKUP)(macroID);
        return true;
    }
    if (const uint32_t setID = EquipmentSet::Action::WordSetID(word))
        return EquipmentSet::Action::Pickup(setID);
    return false;
}

// EquipmentSet::Action's PlaceSet for one of our slots.
void PlaceSet(int slot0, uint32_t setID) {
    if (reinterpret_cast<EditAllowed_t>(Offsets::FUN_CURSOR_EDIT_ALLOWED)(9) == 0)
        return;
    const uint32_t word = Offsets::ACTION_TYPE_EQUIPMENT_SET | setID;
    const uint32_t old = Extended::Get(slot0);
    ::Cursor::Info::Clear();
    if (EquipmentSet::Data::FindByID(setID) == nullptr || old == word)
        return;
    ToCursor(slot0, old);
    Extended::Set(slot0, word);
}

// The item filter at 0x004E6571..0x004E6598.
bool Placeable(const uint8_t *record) {
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
        if (Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4) != 0 &&
            Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + i * 4) ==
                Offsets::ITEM_SPELLTRIGGER_ON_USE)
            return true;
    }
    return Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) != 0;
}

} // namespace

bool CursorWouldPlace() {
    return Game::Read<uint32_t>(Offsets::VAR_CURSOR_SPELL_ID) != 0 || CursorItemGuid() != 0 ||
           Game::Read<uint32_t>(Offsets::VAR_CURSOR_MACRO_ID) != 0 || CursorActionItem() != 0;
}

void Place(int slot0) {
    if (!Extended::Owns(slot0) || !Extended::Ready())
        return;
    if (const uint32_t setID = EquipmentSet::Action::CursorSetID())
        return PlaceSet(slot0, setID);
    if (reinterpret_cast<EditAllowed_t>(Offsets::FUN_CURSOR_EDIT_ALLOWED)(9) == 0)
        return;

    const uint32_t spell = Game::Read<uint32_t>(Offsets::VAR_CURSOR_SPELL_ID);
    uint32_t item = CursorActionItem();
    if (item == 0) {
        if (const uint64_t guid = CursorItemGuid())
            item = ::Object::Entry(::Object::ByGuid(Offsets::TYPEMASK_ITEM, guid));
    }
    const uint32_t macro = Game::Read<uint32_t>(Offsets::VAR_CURSOR_MACRO_ID);
    const uint32_t old = Extended::Get(slot0);

    uint32_t word = 0;
    if (static_cast<int>(spell) > 0) {
        const uint8_t *record = ::Spell::Lookup::RecordForID(static_cast<int>(spell));
        if (record == nullptr)
            return;
        if ((Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
             Offsets::SPELL_ATTR_PASSIVE) != 0) {
            reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(
                Offsets::GAME_ERROR_PASSIVE_ABILITY);
            return;
        }
        if (old == spell) { // a spell word is the id itself
            reinterpret_cast<CursorClearIfSpell_t>(Offsets::FUN_CURSOR_CLEAR_IF_SPELL)();
            return;
        }
        word = spell;
    } else if (static_cast<int>(item) > 0) {
        if (Unit::Identity::PlayerObject() == nullptr)
            return;
        const uint8_t *record = Item::PeekRecord(item);
        if (record == nullptr || !Placeable(record))
            return;
        if (ItemOf(old) == item) {
            ClearCursor();
            return;
        }
        word = item | Offsets::ACTION_TYPE_ITEM_BY_ID;
    } else if (macro != 0) {
        if (MacroOf(old) == macro) {
            ClearCursor();
            return;
        }
        word = macro | Offsets::ACTION_TYPE_BAG_OR_MACRO;
    } else {
        return;
    }

    if (!ToCursor(slot0, old))
        ClearCursor();
    Extended::Set(slot0, word);
}

void Pickup(int slot0) {
    if (!Extended::Owns(slot0) || !Extended::Ready())
        return;
    if (CursorWouldPlace() || EquipmentSet::Action::CursorSetID() != 0)
        return Place(slot0);
    const uint32_t word = Extended::Get(slot0);
    if (word == 0)
        return;
    ToCursor(slot0, word);
    Extended::Set(slot0, 0);
}

} // namespace Action::ExtendedDrag
