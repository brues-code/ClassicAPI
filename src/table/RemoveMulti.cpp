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

// `table.removemulti(t [, pos [, count]])` — WoW's addition to the table
// library (4.3.4's tremovemulti, ltablib.c): removes `count` elements (1 by
// default) from position `pos` (the last element by default), shifts the
// rest down and returns the removed elements in order.
//
// 5.0 keeps a table's length apart from its keys, so like the engine's own
// table.remove (0x7FB770) this records the new length with luaL_setn.
//
// Registered on BOTH states (language-level helper, both are 5.0).

#include "Game.h"
#include "Offsets.h"

namespace Table::RemoveMulti {

namespace {

using luaL_getn_t = int(__fastcall *)(void *L, int t);

int __fastcall Script_table_removemulti(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        Game::Lua::ArgError(L, 1, "table expected");
    const int e = reinterpret_cast<luaL_getn_t>(Offsets::LUAL_GETN)(L, 1);
    int pos = static_cast<int>(Game::Lua::OptNumber(L, 2, e));
    const int n = static_cast<int>(Game::Lua::OptNumber(L, 3, 1));
    if (e == 0)
        return 0;
    if (pos <= 0 || n < 0 || pos > e || pos + n - 1 > e)
        Game::Lua::Error(L, "parameters out of bounds");
    if (!Game::Lua::CheckStack(L, n))
        Game::Lua::Error(L, "stack overflow");
    for (int i = pos; i < pos + n; i++)
        Game::Lua::RawGetI(L, 1, i);
    for (; pos < e; pos++) {
        Game::Lua::RawGetI(L, 1, pos + n);
        Game::Lua::RawSetI(L, 1, pos);
    }
    for (; pos < e + n; pos++) {
        Game::Lua::PushNil(L);
        Game::Lua::RawSetI(L, 1, pos);
    }
    Game::Lua::SetN(L, 1, e - n);
    return n;
}

void Register() {
    Game::Lua::RegisterTableFunction("table", "removemulti", &Script_table_removemulti);
}

const Game::ModuleAutoRegister _autoreg{&Register};
const Game::GlueModuleAutoRegister _autoregGlue{&Register};

} // namespace

} // namespace Table::RemoveMulti
