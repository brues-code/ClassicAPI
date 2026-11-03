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

// `scrub(...)` — returns its arguments, each one that is not a boolean,
// number or string replaced by nil. 4.3.4's Script_scrub.
//
// Registered on both Lua states, like the base-library helpers.

#include "Game.h"

namespace BaseLib::Scrub {

namespace {

namespace Lua = Game::Lua;

int __fastcall Script_scrub(void *L) {
    const int top = Lua::GetTop(L);
    Lua::CheckStack(L, 1);
    for (int i = 1; i <= top; i++) {
        const int type = Lua::Type(L, i);
        if (type != Lua::TYPE_BOOLEAN && type != Lua::TYPE_NUMBER && type != Lua::TYPE_STRING) {
            Lua::PushNil(L);
            Lua::Replace(L, i);
        }
    }
    return top;
}

const Game::Doc::Field kScrubArgs[] = {
    Game::Doc::Vararg("any"),
};
const Game::Doc::Field kScrubRets[] = {
    Game::Doc::Vararg("any", "Each argument, or nil when it is not a boolean, number or string."),
};
const Game::Doc::Function kScrub{
    "Returns its arguments, with each one that is not a boolean, number or string replaced by nil.",
    kScrubArgs, kScrubRets, "LuaGlobals"};

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("scrub", &Script_scrub, &kScrub);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("scrub", &Script_scrub, &kScrub);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::Scrub
