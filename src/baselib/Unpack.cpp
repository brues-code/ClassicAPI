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

// `unpack(list [, i [, j]])` — upgrade 1.12's Lua 5.0 `unpack` to the 5.1
// signature. 5.0's unpack takes NO range arguments and silently ignores
// extras, so 5.1 code calling `unpack(t, 2)` got the WHOLE table back —
// wrong values with no error, the worst failure mode (verified in-game:
// `unpack({10,20,30}, 2)` returned 10, 20, 30). This replacement is a
// port of 5.1's `luaB_unpack` (lbaselib.c): `i` defaults to 1 and `j` to
// the table length via luaL_getn — which reads a `t.n` field first, so
// the transpiler's `unpack(arg)` vararg expansion keeps its
// embedded-nil-aware count exactly as before.
//
// Registered on BOTH states (language-level helper, both are 5.0).

#include "Game.h"
#include "Offsets.h"

namespace BaseLib::Unpack {

namespace {

// Engine auxlib/API entries (see Offsets.h for derivations).
using LuaLGetN_t = int(__fastcall *)(void *L, int idx);
using RawGetI_t = void(__fastcall *)(void *L, int idx, int n);

// Range argument at `idx`: absent/nil → `fallback`; a number (or numeric
// string, matching luaL_optint's coercion) → its value; anything else
// errors like 5.1's luaL_optint.
int OptRange(void *L, int idx, int fallback) {
    if (Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL) // none or nil
        return fallback;
    if (!Game::Lua::IsNumber(L, idx)) {
        Game::Lua::Error(L, "bad argument #%d to 'unpack' (number expected)", idx);
        return 0; // unreachable
    }
    return static_cast<int>(Game::Lua::ToNumber(L, idx));
}

int __fastcall Script_unpack(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "bad argument #1 to 'unpack' (table expected)");
        return 0; // unreachable
    }
    const int i = OptRange(L, 2, 1);
    const int e = OptRange(
        L, 3, reinterpret_cast<LuaLGetN_t>(Offsets::LUAL_GETN)(L, 1));
    if (i > e)
        return 0;
    // `n <= 0` guards the int overflow of `e - i + 1` (i <= e holds here),
    // mirroring 5.1's luaB_unpack.
    const int n = e - i + 1;
    if (n <= 0 || Game::Lua::CheckStack(L, n) == 0) {
        Game::Lua::Error(L, "too many results to unpack");
        return 0; // unreachable
    }
    const auto rawGetI = reinterpret_cast<RawGetI_t>(Offsets::LUA_RAWGETI);
    for (int k = i; k <= e; ++k)
        rawGetI(L, 1, k);
    return n;
}

const Game::Doc::Field kUnpackArgs[] = {
    Game::Doc::Req("list", "table"),
    Game::Doc::Opt("i", "number", "1", "The first index to return."),
    Game::Doc::Opt("j", "number", nullptr,
                   "The last index to return; the length of list, as table.getn gives it, by "
                   "default."),
};
const Game::Doc::Field kUnpackRets[] = {
    Game::Doc::Vararg("any", "list[i] through list[j]; nothing when i is greater than j."),
};
const Game::Doc::Function kUnpack{"Returns the elements of a table from index i to index j.",
                                  kUnpackArgs, kUnpackRets, "LuaGlobals"};

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("unpack", &Script_unpack, &kUnpack);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("unpack", &Script_unpack, &kUnpack);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::Unpack
