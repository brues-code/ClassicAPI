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

// `FlashClientIcon([briefly])` — flashes the client's taskbar button via
// Win32 `FlashWindowEx` on the engine's own game window
// (`FUN_OS_GUI_GET_WINDOW(0)`; benilla-re core/input "Message filter and
// idle sleep"), so this keeps working across the engine's window
// recreations (see `Input::Modifier` — a cached HWND would go stale).
//
//   - `briefly` omitted/false: `FLASHW_ALL | FLASHW_TIMERNOFG` — flashes
//     until the window comes to the foreground, then Windows stops it on
//     its own, so there is nothing to un-flash on focus.
//   - `briefly` true: `FLASHW_ALL` with `uCount` 3 — three flashes, then it
//     stops by itself.
//
// (`dwTimeout` 0 is the default cursor-blink rate in both cases.)
//
// No-op when the handle is null or the window is already foreground —
// there is nobody to notify. Returns nothing.

#include "Game.h"
#include "Offsets.h"

#include <windows.h>

namespace Screen::Flash {

namespace {

using OsGuiGetWindow_t = HWND(__fastcall *)(int which);

int __fastcall Script_FlashClientIcon(void *L) {
    const bool briefly = Game::Lua::ToFlag(L, 1, false);
    const HWND hwnd =
        reinterpret_cast<OsGuiGetWindow_t>(Offsets::FUN_OS_GUI_GET_WINDOW)(0);
    if (hwnd == nullptr || hwnd == GetForegroundWindow())
        return 0;
    FLASHWINFO info{};
    info.cbSize = sizeof(info);
    info.hwnd = hwnd;
    info.dwFlags = briefly ? FLASHW_ALL : (FLASHW_ALL | FLASHW_TIMERNOFG);
    info.uCount = briefly ? 3 : 0;
    info.dwTimeout = 0;
    FlashWindowEx(&info);
    return 0;
}

const Game::Doc::Field kFlashArgs[] = {
    Game::Doc::Opt("briefly", "bool", "false",
                   "True to flash a few times and stop; otherwise flashes until the client is focused."),
};
const Game::Doc::Function kFlashClientIcon{
    "Flashes the client's taskbar button to get the player's attention.",
    kFlashArgs, {}, "SystemGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("FlashClientIcon", &Script_FlashClientIcon,
                                      &kFlashClientIcon);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Screen::Flash
