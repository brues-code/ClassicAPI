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

// `GetTickTime()` — the seconds the current frame advanced: the same value
// the frame's `OnUpdate` scripts receive as `elapsed`. Read from the engine's
// own frame-pass delta (`Tick::FrameTick::Elapsed`, which documents when the
// value changes). Retail's SystemTime is `Environment = "All"`, and the frame
// pass runs on the glue screens too, so it is registered in both states.

#include "Game.h"
#include "tick/FrameTick.h"

namespace Time::TickTime {

namespace {

int __fastcall Script_GetTickTime(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(Tick::FrameTick::Elapsed()));
    return 1;
}

const Game::Doc::Field kRets[] = {
    Game::Doc::Req("elapsed", "number", "The seconds since the previous frame."),
};
const Game::Doc::Function kGetTickTime{
    "Returns the time in seconds that the current frame advanced, the same value its OnUpdate scripts receive.",
    {}, kRets, "SystemGlobals"};

void RegisterGame() {
    Game::Lua::RegisterGlobalFunction("GetTickTime", &Script_GetTickTime, &kGetTickTime);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("GetTickTime", &Script_GetTickTime, &kGetTickTime);
}

const Game::ModuleAutoRegister _autoreg{&RegisterGame};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlue};

} // namespace

} // namespace Time::TickTime
