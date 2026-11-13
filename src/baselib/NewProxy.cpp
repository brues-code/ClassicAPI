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
// As in 5.1, a weak table marks the valid metatables (weak[m] = true) and is
// its own metatable with __mode "kv". 5.1 keeps it in the closure's upvalue;
// here it lives in the registry under kValidKey, so newproxy is a plain C
// function that goes through the registrars like every other global. Each
// Lua state (game, glue, and every /reload) gets its own, made on first use.
//
// Registered on BOTH states (language-level helper, both are 5.0).

#include "Game.h"

namespace BaseLib::NewProxy {

namespace {

namespace Lua = Game::Lua;

constexpr const char *kValidKey = "__classicapi_newproxy_metatables";

// Pushes the weak table of valid metatables, creating it on first use.
void PushValidSet(void *L) {
    Lua::PushString(L, kValidKey);
    Lua::RawGet(L, Lua::REGISTRY_INDEX);
    if (Lua::Type(L, -1) == Lua::TYPE_TABLE)
        return;
    Lua::SetTop(L, -2);
    Lua::NewTable(L); // the weak table w
    Lua::PushString(L, "__mode");
    Lua::PushString(L, "kv");
    Lua::RawSet(L, -3);
    Lua::PushValue(L, -1);
    Lua::SetMetatable(L, -2); // w is its own metatable
    Lua::PushString(L, kValidKey);
    Lua::PushValue(L, -2);
    Lua::RawSet(L, Lua::REGISTRY_INDEX); // registry[kValidKey] = w
}

int __fastcall Script_newproxy(void *L) {
    Lua::SetTop(L, 1);
    Lua::NewUserdata(L, 0); // the proxy, at 2
    if (Lua::ToBoolean(L, 1) == 0)
        return 1; // no metatable
    if (Lua::Type(L, 1) == Lua::TYPE_BOOLEAN) {
        Lua::NewTable(L); // a new metatable m, at 3 ...
        PushValidSet(L);
        Lua::PushValue(L, 3);
        Lua::PushBoolean(L, 1);
        Lua::RawSet(L, -3); // ... marked valid: weak[m] = true
        Lua::SetTop(L, 3);
    } else {
        bool valid = false;
        if (Lua::GetMetatable(L, 1)) { // its metatable, at 3
            PushValidSet(L);
            Lua::PushValue(L, 3);
            Lua::RawGet(L, -2);
            valid = Lua::ToBoolean(L, -1) != 0;
            Lua::SetTop(L, 3);
        }
        if (!valid)
            Lua::ArgError(L, 1, "boolean or proxy expected");
    }
    Lua::SetMetatable(L, 2);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("proxy", "any", "false",
                   "False for no metatable, true for a new one, or another proxy to share its "
                   "metatable."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("proxy", "userdata", "An empty userdata."),
};
const Game::Doc::Function kNewproxy{"Returns a new empty userdata, with or without a metatable.",
                                    kArgs, kRets, "LuaGlobals"};

void RegisterInGame() { Lua::RegisterGlobalFunction("newproxy", &Script_newproxy, &kNewproxy); }
void RegisterGlue() { Lua::RegisterGlueFunction("newproxy", &Script_newproxy, &kNewproxy); }

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::NewProxy
