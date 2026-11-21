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
#include "table/Border.h"

namespace Table::FindIndexedMismatch {

// `index = C_TableUtil.FindIndexedMismatch(t1, t2)` as the 3.80.2 client has
// it (0x140D32CF0, registered by 0x140D32F40 in both states): both arguments
// must be tables; n = max(#t2, #t1) by lua_objlen; for i = 1..n, rawgeti both
// and compare with lua_equal (luaV_equalval, so `__eq` runs); the first i that
// differs is the return, else nil. Past the shorter table's border the
// comparison is against nil, so tables of different lengths differ at the
// shorter one's border + 1.
//
// `#` here is Table::Border::Find, the border search our transpiled `#` uses.
// lua_equal is 0x6F3590 (1.12's lua_rawequal at 0x6F3550 has no `__eq`).
// 3.80.2 resets the stack with lua_settop(L, 2) after each pair; so does this.
static int __fastcall Script_FindIndexedMismatch(void *L) {
    Game::Lua::CheckType(L, 1, Game::Lua::TYPE_TABLE);
    Game::Lua::CheckType(L, 2, Game::Lua::TYPE_TABLE);
    const unsigned n2 = Table::Border::Find(Table::Border::TableAt(L, 2));
    const unsigned n1 = Table::Border::Find(Table::Border::TableAt(L, 1));
    const unsigned n = n2 > n1 ? n2 : n1;

    for (unsigned i = 1; i <= n; ++i) {
        Game::Lua::RawGetI(L, 1, static_cast<int>(i));
        Game::Lua::RawGetI(L, 2, static_cast<int>(i));
        const bool equal = Game::Lua::Equal(L, -2, -1) != 0;
        Game::Lua::SetTop(L, 2);
        if (!equal) {
            Game::Lua::PushNumber(L, static_cast<double>(i));
            return 1;
        }
    }
    Game::Lua::PushNil(L);
    return 1;
}

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("t1", "table"),
    Game::Doc::Req("t2", "table"),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("index", "number"),
};
static const Game::Doc::Function kDoc{
    "Returns the first index from 1 to the longer length where the two tables hold unequal "
    "values, or nil.",
    kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_TableUtil", "FindIndexedMismatch", &Script_FindIndexedMismatch,
                                     &kDoc);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
static const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterLuaFunctions};

} // namespace Table::FindIndexedMismatch
