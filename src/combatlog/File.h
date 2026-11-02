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

// The combat log file — 4.3.4's WoWCombatLog.txt line, written while
// LoggingCombat is on to a file per logging session named as retail names it
// (Logs\WoWCombatLog-MMDDYY_HHMMSS.txt). 1.12's own WoWCombatLog.txt keeps
// its chat-text lines.

#include "combatlog/Entry.h"

namespace CombatLog::File {

// SendCombatLogEvents' write: CombatLogEntry::PackEvent's line, through
// CGChat::AddCombatMessage's SLogWrite, while combat logging is on.
void Write(const Entry &entry);

} // namespace CombatLog::File
