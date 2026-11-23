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

// The `enable` console command: `/console enable` turns the console on, as
// launching with -console does.
//
// The 3.80.2 client registers it in its start-up (FUN_1401f50e0, at
// 0x1401f603e: category console, the help text below) only while the console
// is not already allowed (DAT_1466b93a8, which -console sets) and kiosk mode is
// off. Its handler FUN_1401f6170 sets that flag and unregisters `enable`
// (FUN_142a35dc0), unless kiosk mode is on. It does not open the console;
// the console key works from then on.
//
// 1.12's flag is VAR_CONSOLE_ENABLED, which only the console-key handlers read,
// so setting it is all -console does. 1.12 has no kiosk mode. The command is
// registered from the glue and the game passes (the registrar ignores a name it
// already has), each time only while the console is still off, so once used it
// stays gone, as in 3.80.2. Chat reaches it through FrameXML's /console, which
// calls ConsoleExec (console/Shell.cpp).

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Console::Enable {

namespace {

constexpr const char *kName = "enable";

int __fastcall Console_Enable(const char *, const char *) {
    *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(Offsets::VAR_CONSOLE_ENABLED)) = 1;
    Game::Console::UnregisterCommand(kName);
    return 1;
}

void Register() {
    if (Game::Read<uint32_t>(Offsets::VAR_CONSOLE_ENABLED) != 0)
        return;
    Game::Console::RegisterCommand(kName, &Console_Enable, Game::Console::CATEGORY_CONSOLE,
                                   "Enable the console with \"/console enable\" in the chat "
                                   "window. This is the same as launching the client with "
                                   "-console");
}

const Game::GlueModuleAutoRegister _glue{&Register};
const Game::ModuleAutoRegister _game{&Register};

} // namespace

} // namespace Console::Enable
