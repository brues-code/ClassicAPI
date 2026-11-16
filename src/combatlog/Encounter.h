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

// The combat log file's ENCOUNTER_START / ENCOUNTER_END lines, from the world
// bosses the group fights. See Encounter.cpp.

#include "combatlog/Entry.h"

namespace CombatLog::Encounter {

// Sees each entry before its line is written, so an ENCOUNTER_START comes
// before the line that starts the fight.
void Observe(const Entry &entry);

} // namespace CombatLog::Encounter
