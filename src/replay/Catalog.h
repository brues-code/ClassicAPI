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

// The combat logs in the Logs folder, and the encounters in one. A log is
// named by its file name without ".txt" (WoWCombatLog-111626_111246); its
// movement file, when there is one, has the same time in its name.

#include <cstdint>
#include <string>
#include <vector>

namespace Replay::Catalog {

struct Log {
    std::string name;
    uint64_t size = 0;
    int64_t modified = 0; // seconds since 1970
    bool movement = false; // it has a movement file
};

// The combat logs, newest first.
std::vector<Log> List();

// Whether `name` is a combat log's name, so a path built from it stays in
// the Logs folder.
bool ValidName(const std::string &name);

std::string CombatPath(const std::string &name);
std::string MovementPath(const std::string &name);

// The file's size now, 0 when it is missing.
uint64_t Size(const std::string &path);

struct Encounter {
    int id = 0;
    std::string name;
    int difficulty = 0;
    int groupSize = 0;
    int map = 0;
    int success = -1; // ENCOUNTER_END's 1 or 0; -1 when the log has no end
    int64_t startMs = 0; // stamps, as Line::Stamp gives them
    int64_t endMs = 0;
};

struct Index {
    bool ok = false; // the file was read
    uint64_t size = 0; // the size it was read at
    int64_t firstMs = 0; // its first and last stamps
    int64_t lastMs = 0;
    std::vector<Encounter> encounters;
};

// Reads a log for its encounters: each ENCOUNTER_START to its ENCOUNTER_END,
// or to the log's next header or its last line when it has none. Worker
// thread safe.
Index Build(const std::string &name);

} // namespace Replay::Catalog
