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

// `newproxy([false | true | proxy])` — Lua 5.1's empty userdata (luaB_newproxy,
// lbaselib.c). `false` or nothing: no metatable. `true`: a new metatable of
// its own. Another proxy: share that proxy's metatable. Only metatables made
// by `newproxy(true)` count; any other argument raises "boolean or proxy
// expected". 5.0 has userdata and their `__gc`, only not this function.
//
// As in 5.1, the closure's upvalue is a weak table that marks the valid
// metatables (weak[m] = true), and is its own metatable with __mode "kv".
//
// Registered on BOTH states (language-level helper, both are 5.0).

#include "Game.h"

namespace BaseLib::NewProxy {

namespace {

namespace Lua = Game::Lua;

int __fastcall Script_newproxy(void *L) {
    Lua::SetTop(L, 1);
    Lua::NewUserdata(L, 0); // the proxy, at 2
    if (Lua::ToBoolean(L, 1) == 0)
        return 1; // no metatable
    if (Lua::Type(L, 1) == Lua::TYPE_BOOLEAN) {
        Lua::NewTable(L); // a new metatable m ...
        Lua::PushValue(L, -1);
        Lua::PushBoolean(L, 1);
        Lua::RawSet(L, Lua::UpvalueIndex(1)); // ... marked valid: weak[m] = true
    } else {
        bool valid = false;
        if (Lua::GetMetatable(L, 1)) {
            Lua::RawGet(L, Lua::UpvalueIndex(1));
            valid = Lua::ToBoolean(L, -1) != 0;
            Lua::SetTop(L, -2);
        }
        if (!valid)
            Lua::ArgError(L, 1, "boolean or proxy expected");
        Lua::GetMetatable(L, 1);
    }
    Lua::SetMetatable(L, 2);
    return 1;
}

void Register() {
    void *L = Lua::State();
    if (L == nullptr)
        return;
    Lua::PushString(L, "newproxy");
    Lua::NewTable(L); // the weak table w
    Lua::PushString(L, "__mode");
    Lua::PushString(L, "kv");
    Lua::RawSet(L, -3);
    Lua::PushValue(L, -1);
    Lua::SetMetatable(L, -2); // w is its own metatable
    Lua::PushCClosure(L, &Script_newproxy, 1);
    Lua::SetTable(L, Lua::GLOBALS_INDEX);
}

const Game::ModuleAutoRegister _autoreg{&Register};
const Game::GlueModuleAutoRegister _autoregGlue{&Register};

} // namespace

} // namespace BaseLib::NewProxy
