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

// The engine's targeting placement — the reticle a ground-target spell or
// item arms — and the cast dispatch that arms it. Shared by
// `C_Spell.CastAtCursor` / `CastAtUnit`, `C_Item.UseAtCursor` / `UseAtUnit`
// and the secure-button `spell` verb.
//
// A commit path has to know the placement it lands is the one its own
// dispatch armed: the spell dispatcher returns nothing, and an attempt the
// engine refuses early leaves an older reticle (one the player armed by hand)
// in place. So a caller that commits calls `Stop()` before dispatching —
// the same cancel (`0x6e4900`, reason 0x1c) the engine's cast attempt
// performs when a new cast begins — and any placement active afterwards is
// its own.
namespace Spell::Placement {

// True while the engine has an in-flight targeting placement of any kind
// (ground reticle, unit/item/corpse select). `VAR_SPELL_PLACEMENT_STATE != 0`.
bool IsActive();

// Cancels any in-flight placement. No-op when none is active.
void Stop();

// Commits the in-flight ground placement at the cursor's world position, the
// way the engine's own left click on the ground does (`0x492580`: pick state
// 1 only, bound through `0x6e60f0`). Returns true when the cast went out.
// Cancels the placement and returns false when it is not a ground one, when
// the cursor is not on terrain, or when the spell still wants another target
// after the bind. No placement active → false with nothing cancelled.
bool CommitAtCursor();

// `CommitAtCursor` with explicit world coords (x, y, z) in place of the
// cursor raycast. Same guards and the same return.
bool CommitAt(const float coords[3]);

// True when the in-flight placement is a ground one that `CommitAt` would
// land, so a caller that wants the reticle left for the player to click can
// tell that apart from a placement it must not leave armed. A NON-ground
// placement is cancelled here, because it means the engine did not accept
// the unit as a target and the cursor has to recover.
bool GroundPending();

// The shared tail of a cast or use aimed at a unit at `coords`, run after a
// successful dispatch. No placement → the cast already went out on the unit.
// A ground placement is committed at `coords`, or left for the player when
// `placeGround` is false. Returns true when the cast went out or the reticle
// was left up on purpose.
bool FinishAtUnit(const float coords[3], bool placeGround);

// Resolves a spell to a spellbook slot and dispatches the cast through the
// engine. For a ground-target spell the engine enters placement mode; for a
// normal spell it fires immediately. Returns true if the cast was dispatched
// (the spell is in the player's spellbook), false otherwise.
//
// The numeric form casts the *exact* spellID (a specific rank). The name form
// defers to the engine's own resolver, which parses a trailing `(Rank N)` —
// `"Blizzard"` casts the highest known rank, `"Blizzard(Rank 6)"` that rank.
//
// `targetGuid` is the implicit cast target: `0` = the engine's default
// (current selection, or ground placement for AoE); a unit's GUID makes a
// unit-target/normal spell fire directly on that unit (the engine substitutes
// it exactly where it would the current target). Ground spells ignore it and
// still enter placement.
bool DispatchSpellCast(int spellID, uint64_t targetGuid = 0);
bool DispatchSpellCastByName(const char *name, uint64_t targetGuid = 0);

} // namespace Spell::Placement
