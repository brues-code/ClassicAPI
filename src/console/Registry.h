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

// The console command registry, walked the way the engine's `help` command
// walks it: a Storm intrusive list filled by FUN_CONSOLE_COMMAND_REGISTER, in
// registration order. Every CVar is in it too (see
// FUN_CONSOLE_CVAR_COMMAND_HANDLER), so this is the one list of everything the
// console can run.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Console::Registry {

struct Command {
    const char *name;
    const char *help; // may be null
    uintptr_t handler;
    int category;

    // Every CVar shares the one handler the CVar registrar passes.
    bool IsCvar() const { return handler == Offsets::FUN_CONSOLE_CVAR_COMMAND_HANDLER; }
};

// A list node, or 0 once the walk reaches the end. The low bit tags the
// sentinel, the same convention the addon registry and the parser lists use.
inline bool IsNode(uintptr_t node) { return node != 0 && (node & 1) == 0; }

// Calls `fn(const Command &)` for every registered command. Steps to the next
// node before the call, so the walk does not depend on what `fn` does.
template <typename Fn> void ForEach(Fn &&fn) {
    const uintptr_t linkOffset =
        Game::Read<uint32_t>(Offsets::VAR_CONSOLE_COMMAND_LINK_OFFSET);
    uintptr_t node = Game::Read<uint32_t>(Offsets::VAR_CONSOLE_COMMAND_LIST_HEAD);
    while (IsNode(node)) {
        const Command command{
            Game::Read<const char *>(node + Offsets::OFF_CONSOLE_COMMAND_NAME),
            Game::Read<const char *>(node + Offsets::OFF_CONSOLE_COMMAND_HELP),
            Game::Read<uint32_t>(node + Offsets::OFF_CONSOLE_COMMAND_HANDLER),
            Game::Read<int32_t>(node + Offsets::OFF_CONSOLE_COMMAND_CATEGORY),
        };
        node = Game::Read<uint32_t>(node + linkOffset + 4);
        if (command.name != nullptr) // a node the registrar never finished
            fn(command);
    }
}

} // namespace Console::Registry
