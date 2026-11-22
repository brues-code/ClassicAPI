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

// The `boss1`..`boss5` unit tokens: the bosses of the encounter under way
// (CombatLog::Encounter). See Boss.cpp.

#include <cstdint>

namespace Unit::Boss {

// The `bossN` range is `1..kTokenCount`; the 3.80.2 client reads at most
// five units off its encounter list.
constexpr int kTokenCount = 5;

// The unit `bossN` names (`index` 1..kTokenCount), or 0 when there is none.
uint64_t GetGUIDByIndex(int index);

// The `bossN` index (1..kTokenCount) of `guid`, or 0 for none.
int IndexOf(uint64_t guid);

// CombatLog::Encounter calls this after `guid` joined or left its list of
// bosses: moves the unit's token events and fires
// INSTANCE_ENCOUNTER_ENGAGE_UNIT.
void Changed(uint64_t guid);

} // namespace Unit::Boss
