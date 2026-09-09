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

namespace Spell::Lookup {

// Returns the Spell.dbc record pointer for `spellID`, or nullptr if the
// ID is out of range or the slot is empty. Records are 1-based; ID 0 is
// always invalid.
const uint8_t *RecordForID(int spellID);

// True if `spellRecord`'s SpellFamilyName equals `family` and its
// SpellFamilyFlags overlap `flagMask` — the client-side form of the server's
// `SpellEntry::IsFitToFamily<FAMILY, FLAG>()`. `flagMask` must be non-zero
// (the callers always identify a spell by a specific flag bit). Returns false
// for a null record. Callers add any extra discriminators (icon, effect type)
// themselves.
bool IsFitToFamily(const uint8_t *spellRecord, uint32_t family,
                   uint64_t flagMask);

// Resolves a 1-based Lua-facing spellbook slot to the spellID stored
// there. `bookType` is `0` for the player spellbook, `1` for the pet
// spellbook (matches the engine's encoding from `Script_GetSpellName`).
// Returns 0 if the slot is out of range or empty (Lua side surfaces 0
// as nil, since spellID 0 is never valid).
//
// `bookType` strings — the canonical mapping used by 1.12's
// `GetSpellName(slot, bookType)`:
//   - "spell" or anything-not-pet  → 0 (player)
//   - "pet"                          → 1 (pet)
int SpellbookSlotToID(int slot1Based, int bookType);

// Modern `Enum.SpellBookSpellBank` argument at Lua stack `idx` → the engine
// bookType above. `1` (Pet) selects the pet book; a missing arg, a
// non-number, or any other value reads as the player book — the same
// tolerance `C_SpellBook.GetSpellBookItemInfo` established.
int SpellBankArgToBookType(void *L, int idx);

// The modern `(slotIndex, spellBank)` argument pair at Lua stack
// `slotIdx` / `bankIdx` → the spellID in that slot. 0 when the slot arg is
// not a number or the slot is empty / out of range. Pure stack reads — the
// caller may still read other arguments afterwards.
int SpellbookItemArgsToID(void *L, int slotIdx, int bankIdx);

// Inverse of `SpellbookSlotToID` — finds the 1-based slot where a
// given `spellID` lives in the spellbook arrays. Searches the player
// book first (`bookType=0`), then the pet book (`bookType=1`). On a
// match, returns the 1-based slot and writes the matching `bookType`
// to `*outBookType` if it's non-null. Returns 0 if the spellID isn't
// in either book.
int FindSpellbookSlot(int spellID, int *outBookType);

// Resolves a spell NAME to a spellID against the player's (then pet's)
// spellbook — the same scope retail's `GetSpellInfo(name)` uses. The
// match is exact and case-sensitive against the current locale's name
// field (matching retail and ShaguTweaks' `libspell`). When a spell has
// several ranks in the book, the highest rank is returned (the spellbook
// arrays list a spell's ranks in ascending order, so the last match
// wins). Returns 0 for a null/empty name or one the player doesn't know.
int SpellNameToID(const char *name);

// Resolves a 0-based UI slot into the recipe's spellID. Used by both
// tradeskill and craft scrapers — they share the same storage shape:
// `[entriesVar]` is a pointer to a heap-allocated array of recipe-
// entry pointers, with the entry's first u32 holding the spellID.
//
// Pass `entriesVar` + `countVar` matching the UI surface
// (`VAR_TRADESKILL_*` or `VAR_CRAFT_*`). Returns 0 if the UI is
// closed, the index is OOR, the entry slot is null, or the entry's
// spellID is 0.
int RecipeSlotSpellID(uintptr_t entriesVar, uintptr_t countVar, int slotIndex0);

// Walks the recipe's reagent itemID array at
// `spellRecord + OFF_SPELL_REAGENT_ID` and returns the itemID
// of the Nth (1-based) non-zero reagent. Returns 0 if the spell
// record is null, the reagent index is < 1, or we hit a 0 slot
// before reaching N — matching the engine's
// `Script_GetTradeSkillReagentItemLink` bail behavior.
int NthRecipeReagentItemID(const uint8_t *spellRecord, int reagentIndex1);

} // namespace Spell::Lookup
