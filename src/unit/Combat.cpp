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

#include "Game.h"

namespace Unit::Combat {

// `InCombatLockdown()` — always returns `false` in 1.12.1. The
// secure-frame system that combat lockdown gates didn't exist in
// vanilla; the function is provided purely so that addons backported
// from later expansions can call it without erroring. Addons that
// actually want "is the player in combat?" should use
// `UnitAffectingCombat("player")`, which the stock 1.12 engine ships.
static int __fastcall Script_InCombatLockdown(void *L) {
    Game::Lua::PushBool(L, false);
    return 1;
}

static const Game::Doc::Field kInCombatLockdownRets[] = {
    Game::Doc::Req("inCombatLockdown", "bool", "Always false."),
};
static const Game::Doc::Function kInCombatLockdown{
    "Returns whether protected frames are locked for combat; always false.", {},
    kInCombatLockdownRets, "RestrictedActionsGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("InCombatLockdown", &Script_InCombatLockdown,
                                      &kInCombatLockdown);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Combat
