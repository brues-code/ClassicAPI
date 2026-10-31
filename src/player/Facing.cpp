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

// `GetPlayerFacing()` → facing angle of the player in radians (0 =
// north/+Y in WoW's world convention, increasing counter-clockwise), or
// nothing when there's no player (glue / loading). Vanilla 1.12 doesn't
// ship it (only character-create/select facing exist).
//
// The facing comes from the player's `GetFacing` virtual, the same read
// 3.3.5's Script_GetPlayerFacing makes (`target->GetFacing()`), so it is the
// world facing on a transport too (see `Unit::Position::Facing`).

#include "Game.h"
#include "unit/Position.h"

namespace Player::Facing {

namespace {

int __fastcall Script_GetPlayerFacing(void *L) {
    void *player = Unit::Position::ResolveToken("player");
    if (player == nullptr)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(Unit::Position::Facing(player)));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetPlayerFacing", &Script_GetPlayerFacing);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Player::Facing
