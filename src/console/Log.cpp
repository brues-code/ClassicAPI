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

// `AppendLogToFile [filename] [numLines]` — saves the console's output to a
// text file.
//
// 4.3.4 registers this command with the console's own commands and documents
// it as "[filename = ConsoleLogs/Log<Timestamp>.txt] [numLines = all]", but in
// the shipped client it only prints "Access Denied". This implements the
// contract that help text describes, so it is a ClassicAPI extension:
//
//   - `filename` is relative to the client's working directory (where
//     ExportInterfaceFiles writes too); missing directories are created, and
//     an existing file is appended to, as the name says.
//   - `numLines` keeps the most recent N lines; without it, every line.
//   - Lines are written oldest first, one per line.
//
// The lines are the engine's own list, read under the lock ConsoleWrite takes
// to link a line in. The line being typed is not output and is skipped.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "console/Registry.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace Console::Log {

namespace {

// The console's output lines, oldest first, copied out under the console's
// own lock so a concurrent ConsoleWrite cannot change the list mid-walk.
std::vector<std::string> SnapshotLines() {
    std::vector<std::string> lines;
    auto *lock = reinterpret_cast<CRITICAL_SECTION *>(Offsets::VAR_CONSOLE_LINE_LOCK);
    EnterCriticalSection(lock);
    const uintptr_t linkOffset =
        Game::Read<uint32_t>(Offsets::VAR_CONSOLE_LINE_LIST_LINK_OFFSET);
    uintptr_t node = Game::Read<uint32_t>(Offsets::VAR_CONSOLE_LINE_LIST_HEAD);
    bool head = true;
    while (Console::Registry::IsNode(node)) {
        const bool typing =
            head && Game::Read<uint32_t>(node + Offsets::OFF_CONSOLE_LINE_INPUT_POS) != 0;
        const auto *text = Game::Read<const char *>(node + Offsets::OFF_CONSOLE_LINE_BUFFER);
        if (!typing && text != nullptr)
            lines.emplace_back(text);
        head = false;
        node = Game::Read<uint32_t>(node + linkOffset + 4);
    }
    LeaveCriticalSection(lock);
    // The list is newest first.
    return std::vector<std::string>(lines.rbegin(), lines.rend());
}

std::string DefaultFileName() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    char name[64];
    std::snprintf(name, sizeof(name), "ConsoleLogs\\Log%04u%02u%02u_%02u%02u%02u.txt",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return name;
}

int __fastcall Console_AppendLogToFile(const char * /*command*/, const char *args) {
    // [filename] [numLines]
    std::string fileName;
    const char *p = args;
    while (*p == ' ')
        ++p;
    while (*p != '\0' && *p != ' ')
        fileName.push_back(*p++);
    while (*p == ' ')
        ++p;
    const long requested = *p != '\0' ? std::strtol(p, nullptr, 10) : 0;
    if (fileName.empty())
        fileName = DefaultFileName();

    std::vector<std::string> lines = SnapshotLines();
    if (requested > 0 && static_cast<size_t>(requested) < lines.size())
        lines.erase(lines.begin(), lines.end() - requested);

    Common::EnsureParentDirs(fileName.c_str());
    HANDLE file = CreateFileA(fileName.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Game::Console::Printf("AppendLogToFile: could not open %s", fileName.c_str());
        return 1;
    }
    std::string text;
    for (const std::string &line : lines) {
        text += line;
        text += "\r\n";
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written,
                              nullptr) != 0 &&
                    written == text.size();
    CloseHandle(file);
    if (!ok) {
        Game::Console::Printf("AppendLogToFile: could not write %s", fileName.c_str());
        return 1;
    }
    Game::Console::Printf("AppendLogToFile: wrote %u line(s) to %s",
                          static_cast<unsigned>(lines.size()), fileName.c_str());
    return 1;
}

// 4.3.4 registers it with the console's own commands: category console, with
// its help text.
void Register() {
    Game::Console::RegisterCommand(
        "AppendLogToFile", &Console_AppendLogToFile, Game::Console::CATEGORY_CONSOLE,
        "[filename = ConsoleLogs/Log<Timestamp>.txt] [numLines = all]");
}

const Game::GlueModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Console::Log
