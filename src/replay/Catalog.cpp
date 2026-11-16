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

#include "replay/Catalog.h"

#include "replay/Line.h"

#include <windows.h>

#include <algorithm>

namespace Replay::Catalog {

namespace {

constexpr char kCombatPrefix[] = "WoWCombatLog-";
constexpr char kMovementPrefix[] = "WoWMovementLog-";

// FILETIME (100 ns since 1601) to seconds since 1970.
int64_t UnixSeconds(const FILETIME &time) {
    const uint64_t ticks = (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    return static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
}

} // namespace

bool ValidName(const std::string &name) {
    const std::size_t prefix = sizeof kCombatPrefix - 1;
    if (name.size() <= prefix || name.compare(0, prefix, kCombatPrefix) != 0)
        return false;
    return std::all_of(name.begin() + prefix, name.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               c == '_' || c == '-';
    });
}

std::string CombatPath(const std::string &name) { return "Logs\\" + name + ".txt"; }

std::string MovementPath(const std::string &name) {
    return "Logs\\" + std::string(kMovementPrefix) + name.substr(sizeof kCombatPrefix - 1) + ".txt";
}

uint64_t Size(const std::string &path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

std::vector<Log> List() {
    std::vector<Log> logs;
    WIN32_FIND_DATAA found;
    const HANDLE find = FindFirstFileA("Logs\\WoWCombatLog-*.txt", &found);
    if (find == INVALID_HANDLE_VALUE)
        return logs;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        std::string name = found.cFileName;
        name.resize(name.size() - 4); // ".txt"
        if (!ValidName(name))
            continue;
        Log log;
        log.size = (static_cast<uint64_t>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
        log.modified = UnixSeconds(found.ftLastWriteTime);
        log.movement = Size(MovementPath(name)) != 0;
        log.name = std::move(name);
        logs.push_back(std::move(log));
    } while (FindNextFileA(find, &found));
    FindClose(find);
    std::sort(logs.begin(), logs.end(),
              [](const Log &a, const Log &b) { return a.modified > b.modified; });
    return logs;
}

Index Build(const std::string &name) {
    Index index;
    index.size = Size(CombatPath(name));
    Encounter *open = nullptr; // the encounter without its end yet
    int64_t previousMs = 0;
    const auto close = [&](int64_t endMs) {
        if (open != nullptr)
            open->endMs = endMs;
        open = nullptr;
    };
    index.ok = Line::ForEach(CombatPath(name), [&](std::string_view line) {
        int64_t ms;
        std::string_view body;
        if (!Line::Stamp(line, ms, body))
            return true;
        if (index.firstMs == 0)
            index.firstMs = ms;
        index.lastMs = ms;
        // Only the lines below matter; most lines are combat.
        if (body.empty() || (body[0] != 'E' && body[0] != 'C')) {
            previousMs = ms;
            return true;
        }
        if (Line::Is(body, "ENCOUNTER_START")) {
            close(previousMs);
            const Line::Fields f(body);
            Encounter e;
            e.id = f.Int(1);
            e.name = f.Text(2);
            e.difficulty = f.Int(3);
            e.groupSize = f.Int(4);
            e.map = f.Int(5);
            e.startMs = ms;
            index.encounters.push_back(std::move(e));
            open = &index.encounters.back();
        } else if (Line::Is(body, "ENCOUNTER_END")) {
            const Line::Fields f(body);
            if (open != nullptr && open->id == f.Int(1)) {
                open->success = f.Int(5);
                close(ms);
            }
        } else if (Line::Is(body, "COMBAT_LOG_VERSION")) {
            close(previousMs);
        }
        previousMs = ms;
        return true;
    });
    close(index.lastMs);
    return index;
}

} // namespace Replay::Catalog
