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

// `UnitCombatReach(unit)` → the unit's combat reach in yards
// (UNIT_FIELD_COMBATREACH), or nothing when the token has no unit. No
// Blizzard client exposes this; it is here so an addon can turn the 3D
// center distance from `UnitPosition` into the distance spell range
// measures (3D center distance minus both reaches, as IsTargetInSpellRange
// 0x6E47B0 and the server compare it: see `Unit::Position::CombatReach`),
// without a separate distance DLL.

#include "Game.h"
#include "unit/Position.h"

namespace Unit::CombatReach {

namespace {

int __fastcall Script_UnitCombatReach(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitCombatReach(\"unit\")");
        return 0;
    }
    void *unit = Game::ResolveUnitToken(Game::Lua::ToString(L, 1));
    if (unit == nullptr)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(Unit::Position::CombatReach(unit)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken", "The unit to read."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("reach", "number", nullptr,
                   "Yards that spell, melee and interact range add for this unit; nil when the "
                   "unit does not exist."),
};
const Game::Doc::Function kUnitCombatReach{
    "Returns the combat reach of a unit.", kArgs, kRets, "UnitGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitCombatReach", &Script_UnitCombatReach,
                                      &kUnitCombatReach);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::CombatReach
