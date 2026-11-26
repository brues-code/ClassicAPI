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

namespace Spell::Cooldown {

// The engine's own cooldown query (`FUN_SPELL_QUERY_COOLDOWN`) for
// `spellID`, read from the pet's spell history when the spell is in the
// pet's book and not the player's (4.3.4's by-ID `GetSpellCooldown` picks
// the history the same way), else the player's: start tick and duration in engine milliseconds
// (unsigned, `GetTime()` epoch — see `Time::Clock`), and whether the
// cooldown is enabled (false = "on hold"). Both times are 0 when no
// cooldown is active. Returns false without touching the outputs when
// `spellID` has no Spell.dbc row.
bool Query(int spellID, uint32_t *startMs, uint32_t *durationMs,
           bool *enabled);

// Same, read from the history the caller names (`pet` = the pet's) — the
// shape 4.3.4's `Spell_C_GetSpellCooldown(spellId, isPet, …)` takes, for
// callers that are handed the history instead of deriving it.
bool Query(int spellID, bool pet, uint32_t *startMs, uint32_t *durationMs,
           bool *enabled);

// Whether a reported cooldown came from the history's global leg (the
// SpellCooldownInfo `isOnGCD` field): a live global leg of the spell's
// StartRecoveryCategory carrying exactly that start and duration.
bool ReportsGlobalCooldown(int spellID, bool pet, uint32_t startMs, uint32_t durationMs);

// Pushes a SpellCooldownInfo table: startTime / duration in GetTime()
// seconds, isEnabled, isActive (enabled with a start and a duration, as
// 3.80.2's builder FUN_1417fb4a0 sets it), modRate 1, and isOnGCD when
// `isOnGCD` is not null (3.80.2 leaves it nil when no category decided the
// duration). activeCategory / timeUntilEndOfStartRecovery stay nil.
void PushInfo(void *L, uint32_t startMs, uint32_t durationMs, bool enabled, const bool *isOnGCD);

} // namespace Spell::Cooldown
