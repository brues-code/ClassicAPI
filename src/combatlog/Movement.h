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

// The movement file (Logs\WoWMovementLog-<MMDDYY_HHMMSS>.txt), for the other
// writers and the readers of its lines. Movement.cpp documents the file and
// its lines.

#include <cstdint>
#include <string>

namespace CombatLog::Movement {

// MOVEMENT_LOG_VERSION: the lines' format, which readers check.
constexpr int kVersion = 4;

// Whether the movement file is open.
bool IsOpen();

// The line, stamped, while the file is open.
void Emit(const std::string &body);

// Changes each time the file writes its header: when it opens and after each
// loading screen. A reader starts over at a header, so a writer of lines that
// describe state (what each unit looks like) writes them all again.
uint32_t Generation();

} // namespace CombatLog::Movement
