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

// Shared spell-range core. One place for "does this spell have a range"
// and "is the player's cast of this spell in range of a unit", used by
// `C_Spell.SpellHasRange`, `C_Spell.IsSpellInRange`, and (via the item's
// on-use spell) `C_Item.IsItemInRange`.
namespace Spell::Range {

// True iff the engine's own range resolver (`FUN_GET_MIN_MAX_RANGE`)
// reports a non-near-zero min or max for the spell — the same notion
// vanilla's `ActionHasRange` uses. Covers what a raw `SpellRange.dbc`
// read misses: `Attributes & 0x404` spells (flat 100-yard max), melee
// rows whose max folds combat reach, and the stationary / ranged-item /
// spell-mod bonuses. Rangeless spells (self buffs, etc.) have no
// meaningful "in range" answer.
bool SpellIDHasRange(int spellID);

// Range check of the local player's cast of `spellID` against
// `unitToken` (`"target"`, `"mouseover"`, `"party1"`, …). Returns:
//   1  — in range
//   0  — out of range
//  -1  — no answer: `spellID <= 0`, a rangeless spell, a null/absent
//        unit token, or either unit's position can't be read.
// Backed by the engine's geometric range core `FUN_SPELL_RANGE_CHECK`
// (the same test the action-button range coloring uses), which folds in
// the target's bounding radius so the boundary matches the client's own
// "in range" exactly — melee and min-range (too-close) spells included.
// Ignores line of sight and target faction, matching retail's range-only
// `Is*InRange` semantics.
int PlayerVsUnit(int spellID, const char *unitToken);

} // namespace Spell::Range
