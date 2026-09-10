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

namespace Spell::AtCursor {

// True while the engine has an in-flight targeting placement of any kind
// (ground reticle, unit/item/corpse select). `VAR_SPELL_PLACEMENT_STATE != 0`.
// Used by `CastAtUnit` to tell a unit-target spell that fired immediately
// (no placement) from a ground spell that's waiting for a location.
bool IsPlacementActive();

// Resolves the engine's in-flight ground-target placement at the
// player's current cursor world position. Intended to be called
// immediately after initiating a ground-target cast (spell or item)
// to skip the manual click. Decision tree:
//
//   1. No placement active                  → return false (nothing to do)
//   2. Placement is unit-target, not ground → cancel placement, return false
//   3. Cursor not pointed at terrain        → cancel placement, return false
//   4. Cursor on terrain                    → commit at those world coords, return true
//
// Shared by `C_Spell.CastAtCursor` and `C_Item.UseAtCursor`. Safe to
// call from any in-world context — internally gates on a non-NULL
// click-info struct pointer.
bool Resolve();

// Commits an in-flight ground-target placement at explicit world
// coords (x, y, z) instead of the cursor raycast. Same placement
// guards as `Resolve` (no placement active → false; non-ground
// placement → cancel + false; not in world → false), but skips the
// cursor entirely. Used by `C_Spell.CastAtUnit` / `C_Item.UseAtUnit`
// to drop the reticle at a unit's feet. Returns true when the
// placement was committed.
bool CommitAtCoords(const float coords[3]);

// True when the in-flight placement is a GROUND one that `CommitAtCoords`
// would land, so a caller that wants the reticle left for the player to
// click can tell that apart from a placement it must not leave armed. Same
// guards as the commit paths, including the side effect: a NON-ground
// placement is cancelled here, because it means the engine did not accept
// the unit as a target and the cursor has to recover.
bool GroundPlacementPending();

// Resolves a numeric `spellID` to a spellbook slot and dispatches the
// cast through the engine — the shared front half of
// `C_Spell.CastAtCursor` / `CastAtUnit`. For a ground-target spell
// the engine enters placement mode (commit follows via `Resolve` /
// `CommitAtCoords`); for a normal spell it fires immediately. Returns
// true if the cast was dispatched (spell is in the player's
// spellbook), false otherwise.
//
// The numeric form casts the *exact* spellID (a specific rank). The
// name form defers to the engine's own resolver, which parses a
// trailing `(Rank N)` — `"Blizzard"` casts the highest known rank,
// `"Blizzard(Rank 6)"` casts that rank.
//
// `targetGuid` is the implicit cast target: `0` = the engine's default
// (current selection, or ground placement for AoE); a unit's GUID makes
// a unit-target/normal spell fire directly on that unit (the engine
// substitutes it exactly where it would the current target). Ground
// spells ignore it and still enter placement. `CastAtUnit` passes the
// resolved unit's GUID; `CastAtCursor` leaves it 0.
bool DispatchSpellCast(int spellID, uint64_t targetGuid = 0);
bool DispatchSpellCastByName(const char *name, uint64_t targetGuid = 0);

} // namespace Spell::AtCursor
