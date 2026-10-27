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

// The casts just seen — the SMSG_SPELL_GO hit lists of the last second.
//
// 1.12's dispel and instant-kill messages name less than 4.3.4's: a dispel
// says who dispelled what from whom but not with which spell, and an instant
// kill names the victim and the spell but no caster. The server writes each of
// them from the spell's effect handler, in the same tick as, and after, that
// spell's SMSG_SPELL_GO (Spell::cast sends the go before handle_immediate). So
// the cast that caused a message is the newest recorded cast that fits it:
// by caster and target with a dispel effect, or by spell and target.

#include <cstdint>

namespace CombatLog::RecentCasts {

// The most hit units one record keeps.
constexpr int MAX_HITS = 16;

// Keeps a cast's caster, spell and the units it hit.
void Record(uint64_t caster, uint32_t spellId, const uint64_t *hits, int count);

// The spell of the newest recorded cast by `caster` that hit `target` and
// has effect `effectA` or `effectB`, as a Spell.dbc record, or nullptr.
const uint8_t *SpellByEffect(uint64_t caster, uint64_t target, int effectA, int effectB);

// The caster of the newest recorded cast of `spellId` that hit `target`, or 0.
uint64_t CasterOf(uint32_t spellId, uint64_t target);

} // namespace CombatLog::RecentCasts
