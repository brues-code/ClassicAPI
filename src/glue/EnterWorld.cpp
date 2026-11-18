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

#include "glue/EnterWorld.h"

#include "Game.h"
#include "Offsets.h"

namespace Glue::EnterWorld {

namespace {

// FUN_GLUE_ENTER_WORLD is `void __cdecl(void)`.
using EnterWorld_t = void(__cdecl *)();
EnterWorld_t g_origEnterWorld = nullptr;

void __cdecl EnterWorld_h() {
    g_origEnterWorld();
    if (Game::Read<int>(Offsets::VAR_GLUE_LOGIN_INPROGRESS) !=
        Offsets::GLUE_STATE_ENTERING_WORLD)
        return;
    const auto *character = Game::Read<const uint8_t *>(Offsets::VAR_GLUE_SELECTED_CHAR);
    if (character == nullptr)
        return;
    for (auto *node = AutoSubscribe::head; node != nullptr; node = node->next)
        node->fn(character);
}

const Game::HookAutoRegister _hook{Offsets::FUN_GLUE_ENTER_WORLD,
                                   reinterpret_cast<void *>(&EnterWorld_h),
                                   reinterpret_cast<void **>(&g_origEnterWorld)};

} // namespace

} // namespace Glue::EnterWorld
