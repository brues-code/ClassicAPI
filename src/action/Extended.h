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

// Action slots 121..360, which 1.12 does not have. The server stores slots
// 1..120 only (tortoise-wow `MAX_ACTION_BUTTONS = 120`; the notifier's
// CMSG_SET_ACTION_BUTTON carries a u8 slot) and every engine action helper
// bounds-checks `slot < 0x78`, so these live here, per character, in
// `WTF\Account\<acct>\<realm>\<char>\ClassicAPI_ActionSlots.txt`.
//
// 121..180 are retail's normal slots past 1.12's 120; 181..360 are the
// gamepad action storage (`C_GamepadUI.GetFirstGamepadActionStorageSlotIndex`
// answers 181 and the valid range is 181..360 in the camelot client). Both
// bounds are Blizzard's.
//
// A slot holds the engine's own action word (VAR_ACTION_TABLE's packing):
// a spell id (tag 0), `macroID | 0x40000000`, `itemID | 0x80000000`, or
// EquipmentSet's `setID | 0x20000000`; 0 is empty. The server never sees
// these slots, so a set word is kept here like any other.
namespace Action::Extended {

// 0-based bounds: slot0 120 is Lua slot 121, slot0 359 is Lua slot 360.
constexpr int kFirstSlot0 = 120;
constexpr int kEndSlot0 = 360;

// Whether `slot0` is one of ours.
inline bool Owns(int slot0) { return slot0 >= kFirstSlot0 && slot0 < kEndSlot0; }

// The 0-based slot of a Lua slot argument at stack[idx] when it is a number
// naming one of ours, read as the engine's Script_* functions read theirs
// (`ftol(arg) - 1`). False for anything else, which goes to the engine.
bool SlotArg(void *L, int idx, int *slot0);

// Whether the current character's slots are available: in world, with the
// account, realm and character known. Out of world every slot reads empty
// and nothing can be stored.
bool Ready();

// Whether the current character's file is already loaded — Ready without
// resolving it, for engine callbacks that must not read a file.
bool Loaded();

// The word in `slot0` (one of ours), 0 when empty or not Ready.
uint32_t Get(int slot0);

// Stores `word` in `slot0`, writes the file, and runs FUN_ACTION_SLOT_SET's
// tail through Action::ExtendedState::Stored (item recount, usable recompute,
// ACTIONBAR_SLOT_CHANGED(slot0 + 1)). Never sends anything to the server.
void Set(int slot0, uint32_t word);

// FUN_ACTION_SLOT_TO_SPELL (GetActionSlotSpell, 0x004E5A50) for a word of
// ours: a spell is itself (book 0); a live macro is its cached primary spell
// and book (OFF_MACRO_PRIMARY_SPELL / _IS_PET); an item is its first on-use
// spell (book 0), asking the item cache for an unknown item as the engine
// does. 0 for anything else.
uint32_t SpellFor(uint32_t word, uint32_t *book);

// FUN_ACTION_SPELL_ICON_ACTIVE (ActionSlotAuraActive, 0x004E55F0) for a
// spell: its record has a nonzero activeIconID and its own id sits in one of
// the 48 aura slots of the active player (book 0) or the pet (book 1) with
// bit 0 of that slot's flag nibble set.
bool AuraActive(uint32_t spellID, uint32_t book);

// The macro id of a live macro word (its id is in the macro-slot map, the
// test FUN_ACTION_IS_MACRO makes), else 0.
uint32_t LiveMacroID(uint32_t word);

} // namespace Action::Extended
