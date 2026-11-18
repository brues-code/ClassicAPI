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

// The live damage meter: the player's own sessions, fed from the combat log
// entries while the cvar damageMeterEnabled is on. See Live.cpp.

#include "combatlog/Entry.h"
#include "damagemeter/Totals.h"

#include <cstdint>

namespace DamageMeter::Live {

// Sees each combat log entry (CombatLog::Queue), after the encounter observer.
void Observe(const CombatLog::Entry &entry);

// The live meter, and the time on its clock (milliseconds, never wrapping).
const Meter &Sessions();
int64_t NowMs();

// ResetAllCombatSessions: drops every session and fires DAMAGE_METER_RESET.
void ResetAll();

} // namespace DamageMeter::Live
