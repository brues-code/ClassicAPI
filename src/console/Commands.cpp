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

// `ConsoleGetAllCommands()` — every console command and console variable the
// client currently has, as `ConsoleCommandInfo` tables.
//
// A plain global, not a `C_Console` member: the wiki files this under
// `C_Console.GetAllCommands`, but on a live client that namespace does not
// exist and `ConsoleGetAllCommands` is the real name.
//
// The engine keeps them in one registry, filled by
// FUN_CONSOLE_COMMAND_REGISTER: a Storm intrusive list for iteration plus a
// by-name hash for dispatch. This walks the list the same way the `help`
// command does, which is the only other thing that enumerates it.
//
// ONE LIST, NOT TWO. Commands and CVars do not live separately. The CVar
// registrar's last act is to register the CVar as a console command as well —
// `FUN_CONSOLE_COMMAND_REGISTER(cvar->name, <shared handler>, categoryId,
// help)` — which is what makes `/console <cvarName> <value>` work. So the
// command list already holds every CVar, and walking the CVar list too (the one
// `cvarlist` and the config saver use) would report each one twice.
//
// That also gives an exact `commandType`: every CVar shares one handler
// address, so a node pointing at it is a CVar and anything else is a command.
// It is a pointer comparison against a value the registrar itself writes, not a
// name or category heuristic.
//
// FIELDS WE CANNOT FILL. `scriptContents` and `scriptParameters` describe
// console macros and scripts, which this client has no notion of — there is no
// registration path that could produce one. They are always the empty string,
// so a caller reading them gets a string rather than a nil surprise. `category`
// is the engine's own number, and the engine's table (debug, graphics, console,
// combat, game, default, net, sound, gm) is Enum.ConsoleCategory's 0..8 exactly,
// so it needs no translation.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Console::Commands {

namespace {

// Enum.ConsoleCommandType. Macro (2) and Script (3) exist in the contract but
// cannot occur here.
constexpr int kCommandTypeCvar = 0;
constexpr int kCommandTypeCommand = 1;

using ConsoleWrite_t = void(__fastcall *)(const char *line, int colorType);

// A list node, or 0 once the walk reaches the end. The low bit tags the
// sentinel, the same convention the addon registry and the parser lists use.
bool IsNode(uintptr_t node) { return node != 0 && (node & 1) == 0; }

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool StartsWithIgnoreCase(const char *name, const char *prefix) {
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (name[i] == '\0' || Lower(name[i]) != Lower(prefix[i]))
            return false;
    }
    return true;
}

void SetStringField(void *L, const char *key, const char *value) {
    Game::Lua::PushString(L, key);
    Game::Lua::PushString(L, value != nullptr ? value : "");
    Game::Lua::SetTable(L, -3);
}

void SetIntField(void *L, const char *key, int value) {
    Game::Lua::PushString(L, key);
    Game::Lua::PushNumber(L, static_cast<double>(value));
    Game::Lua::SetTable(L, -3);
}

// `ConsoleGetAllCommands() -> ConsoleCommandInfo[]`
int __fastcall Script_GetAllCommands(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const uintptr_t linkOffset =
        Game::Read<uint32_t>(Offsets::VAR_CONSOLE_COMMAND_LINK_OFFSET);
    uintptr_t node = Game::Read<uint32_t>(Offsets::VAR_CONSOLE_COMMAND_LIST_HEAD);

    int count = 0;
    while (IsNode(node)) {
        const auto *name =
            Game::Read<const char *>(node + Offsets::OFF_CONSOLE_COMMAND_NAME);
        const auto *help =
            Game::Read<const char *>(node + Offsets::OFF_CONSOLE_COMMAND_HELP);
        const uintptr_t handler =
            Game::Read<uint32_t>(node + Offsets::OFF_CONSOLE_COMMAND_HANDLER);
        const int category =
            Game::Read<int32_t>(node + Offsets::OFF_CONSOLE_COMMAND_CATEGORY);

        // Step before building the entry: the walk stays correct even if a
        // field read below is skipped.
        node = Game::Read<uint32_t>(node + linkOffset + 4);

        if (name == nullptr)
            continue; // a node the registrar never finished; nothing to report

        Game::Lua::PushNumber(L, ++count);
        Game::Lua::NewTable(L);
        SetStringField(L, "command", name);
        SetStringField(L, "help", help);
        SetIntField(L, "category", category);
        SetIntField(L, "commandType",
                    handler == Offsets::FUN_CONSOLE_CVAR_COMMAND_HANDLER
                        ? kCommandTypeCvar
                        : kCommandTypeCommand);
        SetStringField(L, "scriptContents", "");
        SetStringField(L, "scriptParameters", "");
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// Both enums are Blizzard's, stated in full rather than derived from the
// engine's own category table. They are an API contract, not a description of
// what this client happens to have: an addon asking for a value we can never
// return still deserves the number instead of a nil. `Macro` and `Script` have
// no registration path here, and `Reveal` / `None` are not among the engine's
// nine categories -- everything else lines up, which is the point.
// `ConsolePrintAllMatchingCommands(prefix)` — write every command whose name
// starts with `prefix` to the console, the way its own tab completion lists
// candidates. Case-insensitive, matching how the engine resolves a command
// name.
//
// Prints nothing for an empty prefix rather than dumping all 500-odd entries;
// ConsoleGetAllCommands is the way to ask for everything.
int __fastcall Script_ConsolePrintAllMatchingCommands(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const char *prefix = Game::Lua::ToString(L, 1);
    if (prefix == nullptr || prefix[0] == '\0')
        return 0;

    auto write = reinterpret_cast<ConsoleWrite_t>(Offsets::FUN_CONSOLE_WRITE);
    const uintptr_t linkOffset =
        Game::Read<uint32_t>(Offsets::VAR_CONSOLE_COMMAND_LINK_OFFSET);
    uintptr_t node = Game::Read<uint32_t>(Offsets::VAR_CONSOLE_COMMAND_LIST_HEAD);
    while (IsNode(node)) {
        const auto *name =
            Game::Read<const char *>(node + Offsets::OFF_CONSOLE_COMMAND_NAME);
        node = Game::Read<uint32_t>(node + linkOffset + 4);
        if (name != nullptr && StartsWithIgnoreCase(name, prefix))
            write(name, 0);
    }
    return 0;
}

const Game::Lua::EnumIntegerEntry kCommandTypeEntries[] = {
    {"Cvar", kCommandTypeCvar},
    {"Command", kCommandTypeCommand},
    {"Macro", 2},
    {"Script", 3},
};

// The engine's table at VAR_CONSOLE_CATEGORY_TABLE holds ids 0..8 named debug,
// graphics, console, combat, game, default, net, sound, gm, in that order --
// so a `category` read off a command node is already one of these.
const Game::Lua::EnumIntegerEntry kCategoryEntries[] = {
    {"Debug", 0},   {"Graphics", 1}, {"Console", 2}, {"Combat", 3},
    {"Game", 4},    {"Default", 5},  {"Net", 6},     {"Sound", 7},
    {"Gm", 8},      {"Reveal", 9},   {"None", 10},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("ConsoleGetAllCommands", &Script_GetAllCommands);
    Game::Lua::RegisterGlobalFunction("ConsolePrintAllMatchingCommands",
                                      &Script_ConsolePrintAllMatchingCommands);
    Game::Lua::RegisterIntegerEnum("Enum", "ConsoleCommandType", kCommandTypeEntries,
                                   sizeof(kCommandTypeEntries) /
                                       sizeof(kCommandTypeEntries[0]));
    Game::Lua::RegisterIntegerEnum("Enum", "ConsoleCategory", kCategoryEntries,
                                   sizeof(kCategoryEntries) /
                                       sizeof(kCategoryEntries[0]));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Console::Commands
