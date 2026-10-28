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
#include "expansion/Level.h"

namespace Expansion::Level {

namespace {

// `GetClassicExpansionLevel()` — returns the live expansion as a
// number. Addons backported from Classic Era / Cata Classic gate
// features on this (`if GetClassicExpansionLevel() >= LE_EXPANSION_TBC then ...`);
// we always answer 0 so those gates resolve to the vanilla branch.
int __fastcall Script_GetClassicExpansionLevel(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(Expansion::kCurrentExpansionLevel));
    return 1;
}

// `ClassicExpansionAtLeast(level)` — boolean `currentLevel >= level`.
// Errors on non-numeric input to match modern semantics (the function
// has no useful "undefined" return).
int __fastcall Script_ClassicExpansionAtLeast(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: ClassicExpansionAtLeast(expansionLevel)");
        return 0;
    }
    const int level = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, Expansion::kCurrentExpansionLevel >= level);
    return 1;
}

// `ClassicExpansionAtMost(level)` — boolean `currentLevel <= level`.
int __fastcall Script_ClassicExpansionAtMost(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: ClassicExpansionAtMost(expansionLevel)");
        return 0;
    }
    const int level = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, Expansion::kCurrentExpansionLevel <= level);
    return 1;
}

// `GetExpansionLevel()` / `GetAccountExpansionLevel()` — 4.3.4 reads the
// account's and the active expansion off the connection; a 1.12 client has
// only the base game, so both are the current level.
int __fastcall Script_GetExpansionLevel(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(Expansion::kCurrentExpansionLevel));
    return 1;
}

// `GetMaxPlayerLevel()`.
int __fastcall Script_GetMaxPlayerLevel(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(Expansion::kMaxPlayerLevel));
    return 1;
}

const Game::Doc::Field kExpansionLevelRets[] = {
    Game::Doc::Req("expansionLevel", "number", "LE_EXPANSION_CLASSIC (0)."),
};
const Game::Doc::Function kGetExpansionLevel{
    "Returns the expansion level of the game.", {}, kExpansionLevelRets, "ExpansionGlobals"};
const Game::Doc::Function kGetAccountExpansionLevel{
    "Returns the expansion level of the account.", {}, kExpansionLevelRets,
    "ExpansionGlobals"};

const Game::Doc::Field kMaxPlayerLevelRets[] = {
    Game::Doc::Req("maxLevel", "number", "60."),
};
const Game::Doc::Function kGetMaxPlayerLevel{
    "Returns the highest level a character can reach.", {}, kMaxPlayerLevelRets,
    "ExpansionGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetExpansionLevel", &Script_GetExpansionLevel,
                                      &kGetExpansionLevel);
    Game::Lua::RegisterGlobalFunction("GetAccountExpansionLevel", &Script_GetExpansionLevel,
                                      &kGetAccountExpansionLevel);
    Game::Lua::RegisterGlobalFunction("GetMaxPlayerLevel", &Script_GetMaxPlayerLevel,
                                      &kGetMaxPlayerLevel);
    Game::Lua::RegisterGlobalFunction("GetClassicExpansionLevel",
                                      &Script_GetClassicExpansionLevel);
    Game::Lua::RegisterGlobalFunction("ClassicExpansionAtLeast",
                                      &Script_ClassicExpansionAtLeast);
    Game::Lua::RegisterGlobalFunction("ClassicExpansionAtMost",
                                      &Script_ClassicExpansionAtMost);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Expansion::Level
