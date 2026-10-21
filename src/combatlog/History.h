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

// The combat log's kept entries and filters — 4.3.4's s_entries / s_filters
// and the Lua functions that read them (UnitCombatLog_C_Scripts.cpp).

#include "combatlog/Entry.h"

namespace CombatLog::History {

// QueueCombatLogEntry's link: keeps a copy of the entry, first letting go of
// the oldest one when it is older than the retention time.
void Record(const Entry &entry);

// CombatLogEntry::MatchesFilter: true when no filter is set or one matches.
bool MatchesFilter(const Entry &entry);

} // namespace CombatLog::History
