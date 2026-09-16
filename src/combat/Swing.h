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

// `Enum.PlayerSwingType` values, matching the `_classic_beta_`
// `SwingTimerDocumentation.lua` contract exactly (MainHand=0, OffHand=1,
// Ranged=2). Shared between `Combat::Swing` (PLAYER_SWING) and
// `Combat::SwingRange` (C_SwingTimer / PLAYER_SWING_RANGE_UPDATE).

namespace Combat::Swing {

enum SwingType : int { MAIN_HAND = 0, OFF_HAND = 1, RANGED = 2 };

// Live read of the player's current (already-hasted) swing time for `type`,
// in ms — `UNIT_FIELD_BASEATTACKTIME` / `_OFFHAND` / `UNIT_FIELD_RANGEDATTACKTIME`
// off the player's own descriptor, the same fields `UnitAttackSpeed` /
// `UnitRangedDamage` read. 0 when the player object isn't resolvable or no
// weapon is equipped for that hand (no off-hand weapon, no ranged weapon) —
// callers treat 0 as "this swing type doesn't apply right now".
uint32_t AttackTimeMs(SwingType type);

// True if `player` and `target` (raw CGUnit* object pointers, e.g. from
// `Unit::Position::ResolveToken` or `Object::ByGuid`) are within pure 2D
// (X/Y) melee reach of each other -- the server's own auto-attack distance
// test (`Unit::CanReachWithMeleeAutoAttackAtPosition` /
// `WorldObject::CanReachWithMeleeSpellAttack`, both explicitly 2D; see
// `Offsets::VAR_MELEE_REACH_LEEWAY`'s comment for why the generic 3D spell-
// range core can't be reused here). False if either pointer is null or
// either position can't be read. Shared by `Combat::Swing` (to decide
// whether an attack-start reset is close enough to promise a real swing
// time) and `Combat::SwingRange` (the melee `C_SwingTimer` check).
bool InMeleeRange(void *player, void *target);

// Extension point for server-specific exceptions to the "does casting this
// spell reset the caster's melee swing timer" rule. The stock rule is
// data-driven off the client's own `Spell.dbc` (`InterruptFlags &
// SPELL_INTERRUPT_FLAG_AUTOATTACK`, suppressed by `AttributesEx2 &
// SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS`) — see `src/turtle/SwingReset.cpp`
// for the motivating case: every rank of Slam in Turtle's OWN client DBC
// carries the suppression flag, but Turtle's server doesn't honor it for
// Slam (verified in-game by packet timestamps: both melee timers delayed
// after a Slam cast), contradicting both the client data and the public
// tortoise-wow reference source. A registered predicate returning true
// forces the reset regardless of the `AttributesEx2` suppression bit — it
// does NOT bypass the `InterruptFlags` gate itself, since every known case
// so far already has that bit set.
//
// Declare a file-scope `static const Combat::Swing::AutoSwingResetOverride
// _r{&Predicate};` — same static-init chaining as every other
// `AutoSubscribe`-style registrar in this codebase.
using SwingResetOverride = bool (*)(uint32_t spellId);
struct AutoSwingResetOverride {
    explicit AutoSwingResetOverride(SwingResetOverride fn);
    SwingResetOverride fn;
    AutoSwingResetOverride *next;
};

} // namespace Combat::Swing
