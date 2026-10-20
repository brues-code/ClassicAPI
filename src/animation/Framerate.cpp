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


// `SetMaxAnimFramerate(framesPerSec)` / `GetMaxAnimFramerate()` - 4.3.4's
// globals over CSimpleAnim::s_frameInterval (SimpleFrameScript_Scripts.cpp),
// the throttle that Anim::UpdateProgress steps progress by. The setter stores
// 1 / fps unchecked, as 4.3.4 does: 0 stores infinity, so progress stops
// stepping until the animation ends.

#include "Classes.h"
#include "Game.h"

namespace Animation {

float g_frameInterval = 1.0f / 30.0f;

namespace {

int __fastcall Script_SetMaxAnimFramerate(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: SetMaxFramerate(framesPerSec)");
        return 0;
    }
    g_frameInterval = 1.0f / static_cast<float>(Game::Lua::ToNumber(L, 1));
    return 0;
}

int __fastcall Script_GetMaxAnimFramerate(void *L) {
    Game::Lua::PushNumber(L, 1.0f / g_frameInterval);
    return 1;
}

const Game::Doc::Field kSetArgs[] = {Game::Doc::Req("framesPerSec", "number")};
const Game::Doc::Field kGetRets[] = {Game::Doc::Req("framesPerSec", "number")};

const Game::Doc::Function kSet{"Sets how many times per second animations step their progress.", kSetArgs, {},
                               "AnimationGlobals"};
const Game::Doc::Function kGet{"Returns how many times per second animations step their progress.", {}, kGetRets,
                               "AnimationGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetMaxAnimFramerate", &Script_SetMaxAnimFramerate, &kSet);
    Game::Lua::RegisterGlobalFunction("GetMaxAnimFramerate", &Script_GetMaxAnimFramerate, &kGet);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Animation
