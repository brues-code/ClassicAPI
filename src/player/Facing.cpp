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
// The orientation is a field in the player's CGUnit movement block
// (`OFF_UNIT_FACING` = +0x9C4), sitting just past the {x,y,z} position at
// +0x9B8. The `GetPosition` virtual only copies x/y/z into a caller buffer,
// so the facing is read off the object directly.

#include "Game.h"
#include "Offsets.h"
#include "unit/Position.h"

#include <cstdint>

namespace Player::Facing {

namespace {

int __fastcall Script_GetPlayerFacing(void *L) {
    void *player = Unit::Position::ResolveToken("player");
    if (player == nullptr)
        return 0;

    const float facing = *reinterpret_cast<const float *>(
        static_cast<const uint8_t *>(player) + Offsets::OFF_UNIT_FACING);
    Game::Lua::PushNumber(L, static_cast<double>(facing));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetPlayerFacing", &Script_GetPlayerFacing);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Player::Facing
