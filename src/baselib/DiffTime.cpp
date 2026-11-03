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

// `difftime(t2 [, t1])` — the seconds from `t1` (0 by default) to `t2`, the
// companion of the engine's `date` and `time` globals. 4.3.4's
// Script_difftime: C `difftime` over the two arguments as time_t.
//
// Registered on BOTH states, as the engine registers `date` and `time`.

#include "Game.h"

#include <ctime>

namespace BaseLib::DiffTime {

namespace {

int __fastcall Script_difftime(void *L) {
    const auto t2 = static_cast<time_t>(Game::Lua::CheckNumber(L, 1));
    const auto t1 = static_cast<time_t>(Game::Lua::OptNumber(L, 2, 0));
    Game::Lua::PushNumber(L, std::difftime(t2, t1));
    return 1;
}

const Game::Doc::Field kDifftimeArgs[] = {
    Game::Doc::Req("time2", "number", "A time in seconds, as `time()` returns."),
    Game::Doc::Opt("time1", "number", "0"),
};
const Game::Doc::Field kDifftimeRets[] = {
    Game::Doc::Req("seconds", "number", "time2 minus time1."),
};
const Game::Doc::Function kDifftime{"The number of seconds from time1 to time2.",
                                    kDifftimeArgs, kDifftimeRets, "LuaGlobals"};

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("difftime", &Script_difftime, &kDifftime);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("difftime", &Script_difftime, &kDifftime);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::DiffTime
