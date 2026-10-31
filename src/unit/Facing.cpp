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

// `UnitFacing(unit)` → the unit's facing in radians (0 = north, increasing
// counter-clockwise, 0..2pi), or nothing when the token has no unit. The
// pre-1.3 global, restored. The facing comes from the unit's `GetFacing`
// virtual (see `Unit::Position::Facing`), the read `GetPlayerFacing` makes
// for the player.

#include "Game.h"
#include "unit/Position.h"

namespace Unit::Facing {

namespace {

int __fastcall Script_UnitFacing(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitFacing(\"unit\")");
        return 0;
    }
    void *unit = Unit::Position::ResolveToken(Game::Lua::ToString(L, 1));
    if (unit == nullptr)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(Unit::Position::Facing(unit)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken", "The unit to read."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("facing", "number", nullptr,
                   "Radians from north, counter-clockwise; nil when the unit does not exist."),
};
const Game::Doc::Function kUnitFacing{
    "Returns the direction a unit faces.", kArgs, kRets, "UnitGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitFacing", &Script_UnitFacing, &kUnitFacing);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Facing
