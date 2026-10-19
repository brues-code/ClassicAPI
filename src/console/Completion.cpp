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

// The two Tab behaviours 4.3.4's console has and 1.12's lacks.
//
// 1.12's Tab completes command NAMES only (FUN_0063FC60). 4.3.4's key handler
// adds two branches in front of that, and this restores both:
//
//   Ctrl+Tab   lists every command whose name CONTAINS the typed text
//              (ConsoleCommandPrintAllMatching with substring = true): sorted,
//              case-insensitive, one per line. With a space in the line it
//              lists the argument completions instead.
//   Tab        with a space in the line, completes the ARGUMENT, cycling
//              forward (Shift+Tab backward) through sorted matches — 4.3.4's
//              ConsoleCommandComplete argument branch.
//
// 4.3.4 drives argument completion from a per-command callback, but no
// command in 4.3.4 registers one, so its argument branch never fires there.
// This gives it the one source that obviously fits — CVar names after `set`,
// `cvar_reset` and `cvar_default`, the commands whose argument is a CVar name.
// That part is a ClassicAPI extension; the mechanics around it are 4.3.4's.
//
// HOW IT GETS IN FIRST, WITHOUT A HOOK. The console's key handler is an
// ordinary event handler registered at priority 7.0, and the engine links a
// new handler in front of the first one whose priority is <= its own. So
// registering ours at the same 7.0 after the console's runs ours first; it
// returns 0 (consumed) for the Tab presses it handles and 1 for everything
// else, which the console then sees exactly as before. The console registers
// during client startup (FUN_0063B460 from 0x00402858), long before the first
// glue boot, which is when this registers — once, since the registrar does
// not dedup.
//
// THE RUN STATE IS THE ENGINE'S. A completion run is the engine's own
// s_completionMode flag: the first Tab sets it, and the console's handler
// clears it on any other key. An argument run sets that flag too, so the
// engine's rule for when a run ends applies to it unchanged, and Tab during an
// engine name-completion run is left to the engine.

#include "Game.h"
#include "Offsets.h"
#include "console/Registry.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Console::Completion {

namespace {

using Handler_t = int(__fastcall *)(const void *data, void *param);
using RegisterEx_t = void(__fastcall *)(int eventId, Handler_t handler, void *param,
                                        float priority);
using IsKeyDown_t = int(__fastcall *)(int key);
using GetInputLine_t = uintptr_t(__fastcall *)();
using MakeCommandCurrent_t = void(__fastcall *)(uintptr_t line, const char *text);
using SetInputString_t = void(__fastcall *)(const char *text);

// 4.3.4's EVENT_DATA_KEY, first two fields.
struct KeyEvent {
    int key;
    uint32_t meta;
};

// The commands whose argument is a CVar name (registered in FUN_0063D380).
const char *const kCvarArgumentCommands[] = {"set", "cvar_reset", "cvar_default"};

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool StartsWithIgnoreCase(const char *text, const char *prefix) {
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (text[i] == '\0' || Lower(text[i]) != Lower(prefix[i]))
            return false;
    }
    return true;
}

bool ContainsIgnoreCase(const char *text, const char *needle) {
    for (; *text != '\0'; ++text) {
        if (StartsWithIgnoreCase(text, needle))
            return true;
    }
    return needle[0] == '\0';
}

// 4.3.4's CompareCompletions: _stricmp order.
void SortNames(std::vector<const char *> &names) {
    std::sort(names.begin(), names.end(),
              [](const char *a, const char *b) { return _stricmp(a, b) < 0; });
}

// 4.3.4's ParseCommand: the name runs to the first space; the arguments are
// the rest with leading spaces skipped and trailing spaces trimmed.
struct Parsed {
    std::string command;
    std::string arguments;
};

Parsed Parse(const char *line) {
    Parsed parsed;
    const char *space = std::strchr(line, ' ');
    if (space == nullptr) {
        parsed.command = line;
        return parsed;
    }
    parsed.command.assign(line, space);
    while (*space == ' ')
        ++space;
    parsed.arguments = space;
    while (!parsed.arguments.empty() && parsed.arguments.back() == ' ')
        parsed.arguments.pop_back();
    return parsed;
}

bool TakesCvarArgument(const std::string &command) {
    for (const char *name : kCvarArgumentCommands) {
        if (_stricmp(command.c_str(), name) == 0)
            return true;
    }
    return false;
}

// The argument completions for `arguments`, sorted — what 4.3.4's completion
// callback would return. Empty for a command that has none.
std::vector<const char *> ArgumentMatches(const Parsed &parsed) {
    std::vector<const char *> matches;
    if (!TakesCvarArgument(parsed.command))
        return matches;
    Console::Registry::ForEach([&](const Console::Registry::Command &command) {
        if (command.IsCvar() && StartsWithIgnoreCase(command.name, parsed.arguments.c_str()))
            matches.push_back(command.name);
    });
    SortNames(matches);
    return matches;
}

// Ctrl+Tab — 4.3.4's ConsoleCommandPrintAllMatching(partial, substring = true).
void PrintAllMatching(const char *partial) {
    std::vector<const char *> matches;
    if (std::strchr(partial, ' ') != nullptr) {
        matches = ArgumentMatches(Parse(partial));
    } else {
        Console::Registry::ForEach([&](const Console::Registry::Command &command) {
            if (ContainsIgnoreCase(command.name, partial))
                matches.push_back(command.name);
        });
        SortNames(matches);
    }
    for (const char *name : matches)
        Game::Console::Printf("%s", name);
}

// The argument run in progress: the line as it was on the run's first Tab, and
// the index into its matches. Valid only while `g_ownRun` is set.
bool g_ownRun = false;
std::string g_partial;
int g_index = -1;

// 4.3.4's ConsoleCommandComplete, argument branch. Writes "command match" to
// `out` and returns true when the index lands on a match; otherwise clamps the
// index and returns false, leaving the line as it is.
bool CompleteArgument(bool backward, std::string &out) {
    const Parsed parsed = Parse(g_partial.c_str());
    const std::vector<const char *> matches = ArgumentMatches(parsed);
    const int count = static_cast<int>(matches.size());

    if (g_index < 0)
        g_index = 0;
    else
        g_index += backward ? -1 : 1;

    if (g_index >= 0 && g_index < count) {
        out = parsed.command + " " + matches[g_index];
        return true;
    }
    if (g_index < 0)
        g_index = 0;
    if (g_index >= count)
        g_index = count - 1;
    return false;
}

int __fastcall OnKeyDown(const void *data, void * /*param*/) {
    const auto *event = static_cast<const KeyEvent *>(data);
    if (event->key != Offsets::KEY_TAB)
        return 1;

    // The console handler's own gates, in its order: its toggle key is its
    // business, and nothing reaches the input line while it is closed or
    // while that key is held.
    const int toggleKey = Game::Read<int32_t>(Offsets::VAR_CONSOLE_TOGGLE_KEY);
    if (event->key == toggleKey)
        return 1;
    if (Game::Read<uint32_t>(Offsets::VAR_CONSOLE_VISIBLE) == 0)
        return 1;
    if (reinterpret_cast<IsKeyDown_t>(Offsets::FUN_GAMEUI_IS_KEY_DOWN)(toggleKey) != 0)
        return 1;

    const uintptr_t line =
        reinterpret_cast<GetInputLine_t>(Offsets::FUN_CONSOLE_GET_INPUT_LINE)();
    const char *typed = Game::Read<const char *>(line + Offsets::OFF_CONSOLE_LINE_BUFFER) +
                        Game::Read<uint32_t>(line + Offsets::OFF_CONSOLE_LINE_INPUT_START);

    if ((event->meta & Offsets::EVENT_META_CTRL) != 0) {
        PrintAllMatching(typed);
        return 0;
    }

    auto &mode = Game::Ref<uint32_t>(Offsets::VAR_CONSOLE_COMPLETION_MODE);
    if (mode == 0)
        g_ownRun = false;
    if (!g_ownRun) {
        if (mode != 0)
            return 1; // the engine's own name-completion run
        if (std::strchr(typed, ' ') == nullptr)
            return 1; // a command name: the engine's completion
        mode = 1;
        g_ownRun = true;
        g_partial = typed;
        g_index = -1;
    }

    std::string completed;
    if (CompleteArgument((event->meta & Offsets::EVENT_META_SHIFT) != 0, completed))
        reinterpret_cast<MakeCommandCurrent_t>(Offsets::FUN_CONSOLE_MAKE_COMMAND_CURRENT)(
            line, completed.c_str());
    // Re-read the buffer: MakeCommandCurrent may have reallocated it.
    reinterpret_cast<SetInputString_t>(Offsets::FUN_CONSOLE_SET_INPUT_STRING)(
        Game::Read<const char *>(line + Offsets::OFF_CONSOLE_LINE_BUFFER));
    return 0;
}

void Register() {
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    reinterpret_cast<RegisterEx_t>(Offsets::FUN_EVENT_REGISTER_EX)(
        Offsets::EVENT_ID_KEYDOWN, &OnKeyDown, nullptr,
        static_cast<float>(Offsets::CONSOLE_EVENT_PRIORITY));
}

const Game::GlueModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Console::Completion
