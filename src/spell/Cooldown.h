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

// The engine's own cooldown query (`FUN_SPELL_QUERY_COOLDOWN`) for the
// local player's `spellID`: start tick and duration in engine milliseconds
// (unsigned, `GetTime()` epoch — see `Time::Clock`), and whether the
// cooldown is enabled (false = "on hold"). Both times are 0 when no
// cooldown is active. Returns false without touching the outputs when
// `spellID` has no Spell.dbc row.
bool Query(int spellID, uint32_t *startMs, uint32_t *durationMs,
           bool *enabled);

} // namespace Spell::Cooldown
