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

// The combat log file — Classic Era's COMBAT_LOG_VERSION 9 lines, written
// while LoggingCombat is on to a file per logging session named as retail
// names it (Logs\WoWCombatLog-MMDDYY_HHMMSS.txt). 1.12's own WoWCombatLog.txt
// keeps its chat-text lines.

#include "combatlog/Entry.h"

#include <cstdint>
#include <string>

namespace CombatLog::File {

// The entry's line, while combat logging is on.
void Write(const Entry &entry);

// A line that is not an entry's (ENCOUNTER_START and the like), given without
// its stamp, while combat logging is on.
void WriteLine(const std::string &body);

// The number of the open log file, counting each one this session opens; 0
// while combat logging is off. A line pair that must land in one file (an
// encounter's start and end) compares it.
uint32_t Session();

// The MMDDYY_HHMMSS in the open file's name. The movement file takes the same
// one, so a reader can pair the two.
const std::string &FileTime();

// Whether advanced logging (the advancedCombatLogging cvar) is on.
bool Advanced();

// Appends `,` and the GUID as the file writes it (Player-0-0000ABCD, ...).
// A unit's string stays the same from one header to the next, also after it
// leaves view.
void AppendGuid(std::string &line, uint64_t guid);

// Appends `,"text"` with the file's quoting, or `,nil` for no text.
void AppendQuoted(std::string &line, const char *text);

// Appends `,` and the value: in hex (0x1f), as an unsigned number, or with
// `decimals` places.
void AppendHex(std::string &line, uint32_t value);
void AppendUnsigned(std::string &line, uint32_t value);
void AppendFixed(std::string &line, float value, int decimals);

} // namespace CombatLog::File
