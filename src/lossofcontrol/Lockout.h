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

namespace LossOfControl {

// The loss-of-control effect currently blocking the local player from
// casting `spellID`, if any: writes its start / end engine ticks (unsigned
// ms, `GetTime()` epoch — see `Time::Clock`) and returns true. As 3.80.2's
// FUN_14197f180 walks its list: an effect whose timer has run out is
// skipped; a blocking effect with no timer answers at once with start = end
// = now; otherwise the blocking effect that ends last wins. A start of 0
// means the end is known but the start is not. Returns false for an unknown
// `spellID`.
//
// Which effects block which spells restates the server's cast gates:
//   SCHOOL_INTERRUPT              → spells of the locked school
//   STUN / FEAR / CONFUSE / CHARM / POSSESS → every spell
//   SILENCE                       → spells with PreventionType SILENCE (1)
//   PACIFY                        → spells with PreventionType PACIFY (2)
//   PACIFYSILENCE                 → both of the above
//   ROOT / DISARM                 → nothing (neither stops a cast)
// except that, like the client's own cast check, a spell granting immunity
// to the effect is not blocked by it (a druid form under Polymorph).
bool LockoutForSpell(int spellID, uint32_t *startMs, uint32_t *endMs);

} // namespace LossOfControl
