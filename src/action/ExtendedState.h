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

// The per-slot state the engine keeps beside its 120 action words, and the
// per-slot tests it makes on them, for Action::Extended's slots 121..360.
//
// The engine keeps three uint[120] tables next to VAR_ACTION_TABLE: the item
// count (VAR_ACTION_ITEM_COUNTS) and the usable / noMana pair
// (VAR_ACTION_USABLE / VAR_ACTION_NO_MANA). This module keeps the same three
// for our slots and fills them at the engine's moments: a stored word
// (FUN_ACTION_SLOT_SET: recount an item, then the notifier), the notifier
// (FUN_ACTION_SLOT_CHANGED_NOTIFY: recompute the pair, ACTIONBAR_SLOT_CHANGED),
// the bulk refresh (FUN_ACTION_UPDATE_USABLE, through Action::UsableChanged)
// and an item recount (FUN_ACTION_RECOUNT_ITEM_SLOTS, co-hooked here).
//
// The word tests are the engine's slot tests with the slot's word handed in
// instead of read from VAR_ACTION_TABLE; each names the function it mirrors.
namespace Action::ExtendedState {

// --- Our slots (slot0 120..359) ---------------------------------------------

// FUN_ACTION_SLOT_SET's tail for a word just stored in `slot0`: recount it
// when it is an item, then Notify.
void Stored(int slot0);

// FUN_ACTION_SLOT_CHANGED_NOTIFY(slot0, send 0, quiet 0) for one of our
// slots: recompute its usable / noMana pair, then ACTIONBAR_SLOT_CHANGED.
void Notify(int slot0);

// The enter-world apply loop's FUN_ACTION_SLOT_SET(slot, word, 0, quiet 1)
// for every slot after Action::Extended loads its file: counts and pairs, no
// event.
void Reload();

// IsUsableAction's cached pair (0x004E5230) for one of our slots.
bool CachedUsable(int slot0, bool *noMana);

// GetActionCount's cached count (0x004E6C70) for one of our slots: the item
// count of an item word, 0 for anything else.
uint32_t CachedCount(int slot0);

// FUN_ACTION_UPDATE_USABLE's loop for our slots: recompute every pair, store
// the ones that changed and hand each to `changed`. Returns how many changed.
using ChangeSink = void (*)(int slot0, bool usable, bool noMana);
int RefreshUsable(ChangeSink changed);

// --- Word tests (any word, both ranges) --------------------------------------

// FUN_ACTION_SLOT_USABLE (GetActionSlotUsable, 0x004E5050) for `word` with
// item count `count`.
bool Usable(uint32_t word, uint32_t count, bool *noMana);

// FUN_ACTION_SLOT_IS_CURRENT / _IS_AUTO_REPEAT / _IS_ATTACK / _IS_EQUIPPED /
// _HAS_RANGE for `word`, and the ranged auto-repeat test 0x004E5A00 the usable
// and icon code make (Attributes RANGED and AttributesEx2 AUTOREPEAT).
bool IsCurrent(uint32_t word);
bool IsAutoRepeat(uint32_t word);
bool IsAttack(uint32_t word);
bool IsRangedRepeat(uint32_t word);
bool IsEquipped(uint32_t word);
bool HasRange(uint32_t word);

// FUN_ACTION_SLOT_IS_CONSUMABLE's item rule (0x004E5250): ammo or thrown, or
// an on-use spell with negative charges; false while the template is not
// cached.
bool IsConsumableItem(uint32_t itemID);

// FUN_ACTION_SLOT_COOLDOWN (GetActionSlotCooldown, 0x004E6CA0) for `word`, in
// engine ms ticks: an item word through FUN_ITEM_QUERY_COOLDOWN, anything else
// through its resolved spell and book (Spell::Cooldown::Query). `spellID` /
// `pet` name the spell the answer came from (0 for an item or no spell) —
// what 3.80.2 reports isOnGCD for.
struct Cooldown {
    uint32_t startMs = 0;
    uint32_t durationMs = 0;
    bool enabled = false;
    int spellID = 0;
    bool pet = false;
};
Cooldown CooldownFor(uint32_t word);

} // namespace Action::ExtendedState
