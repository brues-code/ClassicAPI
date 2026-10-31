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

namespace Table::Wipe {

// `table.wipe(t)` — removes every key from `t`, leaving the table
// empty but keeping its internal capacity. Returns `t` so callers can
// chain (`local cache = table.wipe(cache)`).
//
// Port of Blizzard's 3.3.5 `twipe` at VA `0x00852180` (verified by
// disassembly). The 3.3.5 implementation uses the canonical
// `lua_next` + `rawset(k, nil)` pattern that's "officially undefined
// behavior" per the Lua reference manual — but Blizzard relies on
// the implementation detail that both Lua 5.0 and Lua 5.1's
// `lua_next` walk the table's hash node array linearly, so removing
// the current entry mid-iteration doesn't corrupt the walk. Same
// guarantee holds in 1.12's Lua 5.0 (`luaH_next` at `0x006FA2A0`),
// so the pattern ports verbatim.
//
// One divergence from the 3.3.5 source: 1.12's Lua 5.0 keeps a table's
// `table.insert`/`getn` length out of band from its keys (see the
// `luaL_setn` note below), so after the key-clear loop we must also
// reset that counter — the 5.1 twipe had no such legacy to clear.
//
// Stack discipline matches 3.3.5 to the call:
//   1. `lua_pushnil(L)`           — initial "no key" sentinel for next
//   2. `lua_next(L, 1)`           — pops key, pushes (newKey, value)
//   3. `lua_settop(L, -2)`        — pop value, keep key on top
//   4. `lua_pushvalue(L, -1)`     — duplicate key (one for next iter,
//                                  one to consume in rawset below)
//   5. `lua_pushnil(L); lua_rawset(L, 1)` — `t[keyDup] = nil`
//   6. loop back to step 2 with key on top
static int __fastcall Script_table_wipe(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: table.wipe(table)");
        return 0;
    }

    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, 1) != 0) {
        Game::Lua::SetTop(L, -2);    // pop value
        Game::Lua::PushValue(L, -1); // duplicate key
        Game::Lua::PushNil(L);
        Game::Lua::RawSet(L, 1);     // t[key] = nil
    }

    // Reset the Lua 5.0 length counter. In 5.0 a table's `table.insert`/
    // `getn` length is stored OUT of band from its keys (in a `t.n`
    // field or a weak `sizes[t]` registry entry), so the rawset(nil)
    // loop above — which only clears keys — leaves it at its old value.
    // Without this, `table.insert` after a wipe appends at
    // `getn(t)+1`, past the now-nil slots, and `t[1]` stays nil. 5.1's
    // wipe doesn't need this (no getn legacy), which is why the ported
    // logic looked complete. `luaL_setn(L, 1, 0)` puts it back to 0 so
    // inserts resume at `t[1]`.
    Game::Lua::SetN(L, 1, 0);

    // The table is still at stack[1] and is the result we return.
    Game::Lua::SetTop(L, 1);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kWipeArgs[] = {
    Game::Doc::Req("tbl", "table"),
};
const Game::Doc::Field kWipeRets[] = {
    Game::Doc::Req("tbl", "table", "The same table, now empty."),
};
const Game::Doc::Function kWipe{"Removes every key from a table.", kWipeArgs, kWipeRets,
                                "LuaGlobals"};

// `wipe` is the global alias 3.3.5's FrameScript compat preamble sets next
// to `tinsert` / `tremove` (`wipe = tab.wipe`). 1.12's preamble predates
// table.wipe, so the alias is registered here; retail FrameXML calls it.
static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("table", "wipe", &Script_table_wipe);
    Game::Lua::RegisterGlobalFunction("wipe", &Script_table_wipe, &kWipe);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Table::Wipe
