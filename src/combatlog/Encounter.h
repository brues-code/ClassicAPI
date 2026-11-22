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

// The combat log file's ENCOUNTER_START / ENCOUNTER_END lines, from the
// bosses (world bosses and listed creatures) the group fights. See
// Encounter.cpp.

#include "combatlog/Entry.h"

#include <cstdint>
#include <string>

namespace CombatLog::Encounter {

// Sees each entry before its line is written, so an ENCOUNTER_START comes
// before the line that starts the fight.
void Observe(const Entry &entry);

// Whether an encounter is under way: from its start until its kill, wipe or a
// loading screen.
bool Active();

// Counts encounter starts: it changes each time an encounter starts, and not
// while one runs.
uint32_t Serial();

// The name of the boss whose fight started the encounter under way, or of
// the last one.
const std::string &BossName();

// The `index`th (from 0) boss of the encounter under way, in the order they
// joined it, dead ones included; 0 past the last and while none is under way.
// The units of the `bossN` tokens (Unit::Boss).
uint64_t EngagedBoss(int index);

// The player or a member of its group in view is in combat
// (UNIT_FLAG_IN_COMBAT). Members out of view count as out of combat.
bool GroupFighting();

// The group's size: the raid roster, else the party and the player.
int GroupSize();

// Era's difficultyID for the current map and a group of `groupSize`: 9 for a
// raid of more than 20, 148 for one of 20 or fewer, 1 for a dungeon, 0
// elsewhere. ZONE_CHANGE writes it too.
int Difficulty(int groupSize);

} // namespace CombatLog::Encounter
