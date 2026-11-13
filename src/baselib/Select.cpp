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

// `select(index, ...)` — the Lua 5.1 base-library vararg helper, missing
// from 1.12's Lua 5.0 (it predates the function). Two forms:
//   - `select('#', ...)`  → the number of extra arguments.
//   - `select(n, ...)`    → all arguments from position `n` onward.
//     `n` may be negative to index from the end (`-1` = the last arg).
//
// Ported from Lua 5.1's `luaB_select` (lbaselib.c). The numeric form needs
// no pushes at all: the varargs are already sitting on the stack at
// positions `2..top`, so returning `top - n` hands back exactly the slice
// from `n+1` onward — the same trick the reference implementation uses.
// The `'#'` form pushes the count and returns 1.
//
// Registered on BOTH Lua states (in-game + glue): it's a language-level
// helper, state-agnostic, and both states are 5.0 and equally missing it.

#include "Game.h"

namespace BaseLib::Select {

namespace {

int __fastcall Script_select(void *L) {
    const int n = Game::Lua::GetTop(L);
    if (n < 1) {
        Game::Lua::Error(L, "bad argument #1 to 'select' (number expected, got no value)");
        return 0; // unreachable
    }

    // `select('#', ...)` — count form. Only inspect the string when the
    // arg *is* a string: calling lua_tostring on a number would convert
    // the stack value in place (Lua 5.0/5.1 semantics), corrupting arg 1.
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_STRING) {
        const char *s = Game::Lua::ToString(L, 1);
        if (s != nullptr && s[0] == '#') {
            Game::Lua::PushNumber(L, static_cast<double>(n - 1));
            return 1;
        }
        // A non-'#' string falls through: lua_isnumber accepts numeric
        // strings (e.g. "2"), matching the reference's luaL_checkint
        // coercion; a non-numeric string errors below.
    }

    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "bad argument #1 to 'select' (number expected)");
        return 0; // unreachable
    }
    int i = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (i < 0)
        i = n + i; // negative indexes count back from the last argument
    else if (i > n)
        i = n;
    if (i < 1) {
        Game::Lua::Error(L, "bad argument #1 to 'select' (index out of range)");
        return 0; // unreachable
    }
    // The varargs from position i+1..n are already on the stack; returning
    // their count hands exactly that slice back to the caller.
    return n - i;
}

const Game::Doc::Field kSelectArgs[] = {
    Game::Doc::Req("index", "number",
                   "The first argument to return, negative to count back from the last, or "
                   "\"#\" for the count."),
    Game::Doc::Vararg("any"),
};
const Game::Doc::Field kSelectRets[] = {
    Game::Doc::Vararg("any", "The arguments from index onward, or their count when index is \"#\"."),
};
const Game::Doc::Function kSelect{
    "Returns its extra arguments from a position onward, or the number of them.",
    kSelectArgs, kSelectRets, "LuaGlobals"};

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("select", &Script_select, &kSelect);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("select", &Script_select, &kSelect);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::Select
