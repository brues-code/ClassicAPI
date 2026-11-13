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

// `table.maxn(t)` — Lua 5.1 addition (luaB_maxn, ltablib.c): the largest
// positive numeric key of `t`, or 0 when it has none. Unlike `#`/getn it
// scans EVERY key, so it sees past nil holes — common in Ace3-era code
// for sparse arrays. Port of the 5.1 reference: full `lua_next` walk,
// tracking the maximum numeric key.
//
// Registered on BOTH states (language-level helper, both are 5.0).

#include "Game.h"

namespace Table::Maxn {

namespace {

int __fastcall Script_table_maxn(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "bad argument #1 to 'maxn' (table expected)");
        return 0; // unreachable
    }
    double max = 0.0;
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, 1) != 0) {
        Game::Lua::SetTop(L, -2); // pop value, keep key for the next iter
        // Type-gate before ToNumber: lua_tonumber would convert a numeric
        // STRING key in place, corrupting the key `lua_next` resumes from.
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER) {
            const double k = Game::Lua::ToNumber(L, -1);
            if (k > max)
                max = k;
        }
    }
    Game::Lua::PushNumber(L, max);
    return 1;
}

const Game::Doc::Field kMaxnArgs[] = {
    Game::Doc::Req("t", "table"),
};
const Game::Doc::Field kMaxnRets[] = {
    Game::Doc::Req("max", "number", "0 when the table has no positive numeric key."),
};
const Game::Doc::Function kMaxn{
    "Returns the largest positive numeric key of a table, looking past nil holes.", kMaxnArgs,
    kMaxnRets};

void RegisterInGame() {
    Game::Lua::RegisterTableFunction("table", "maxn", &Script_table_maxn, &kMaxn);
}

void RegisterGlue() {
    // RegisterTableFunction writes to whichever state VAR_LUA_STATE
    // references — the glue state inside this callback.
    Game::Lua::RegisterTableFunction("table", "maxn", &Script_table_maxn, &kMaxn);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace Table::Maxn
