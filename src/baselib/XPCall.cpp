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

// `success, ... = xpcall(f, msgh [, arg1, ...])` — call `f` under `msgh`,
// forwarding the trailing arguments to it.
//
// 1.12's Lua takes only `xpcall(f, msgh)`. Its implementation is explicit
// about it: `lua_settop(L, 2)` throws any further arguments away, then
// `lua_pcall(L, 0, ...)` calls `f` with none. So the argument-forwarding form
// doesn't merely fail — it silently calls `f` with nothing, and a function
// that reads its parameters sees nil. (Note this is NOT a 5.0-vs-5.1
// difference: both take `(f, msgh)`. Forwarding arrived later.)
//
// Callers work around it with a capturing closure —
// `xpcall(function() return f(a, b) end, msgh)` — which is what
// `ExecuteFrameScript` in the embedded addon does. That stays correct; this
// just removes the need for it.
//
// Why a C function and not a Lua wrapper over the original: a Lua wrapper
// would allocate a closure AND an `arg` table on every call (see the vararg
// note in FunctionUtil.lua), putting an allocation on any error-guarded path,
// including per-frame ones. `HookSecureFunc`'s `kUnhookableNames` also lists
// `xpcall` precisely because shadowing a core primitive with a Lua closure
// diverges from what C-side callers get. Replacing it with a real C function
// has neither problem, and matches how `select` and `unpack` are backported
// in this directory.
//
// A two-argument call is bit-for-bit the old behavior (zero forwarded args),
// so this is purely additive for existing code.

#include "Game.h"

namespace BaseLib::XPCall {

namespace {

int __fastcall Script_xpcall(void *L) {
    const int top = Game::Lua::GetTop(L);
    if (top < 2) {
        Game::Lua::Error(L, "bad argument #2 to 'xpcall' (value expected)");
        return 0; // unreachable
    }

    // `lua_pcall` wants the callee and its arguments contiguous on top, and
    // the handler at a fixed index. Incoming order is
    // `f(1), msgh(2), a1(3)..aN(top)`, so the handler sits between the two —
    // shuffle it below `f`:
    //
    //   PushValue(2)  →  f, msgh, a1..aN, msgh
    //   Insert(1)     →  msgh, f, msgh, a1..aN     (top moved down to index 1)
    //   Remove(3)     →  msgh, f, a1..aN
    //
    // 5.2 does the same reshuffle with a single `lua_rotate`, which this Lua
    // has no equivalent of.
    Game::Lua::PushValue(L, 2);
    Game::Lua::Insert(L, 1);
    Game::Lua::Remove(L, 3);

    const int status =
        Game::Lua::PCall(L, /*nargs*/ top - 2, Game::Lua::MULTRET, /*errfunc*/ 1);

    // The stack now holds the handler plus either f's results or the handler's
    // return. Both cases want the status boolean in front of them, and the
    // handler itself dropped from the return set — which returning
    // `GetTop - 1` does, since results are taken from the top.
    Game::Lua::PushBool(L, status == 0);
    Game::Lua::Insert(L, 2);
    return Game::Lua::GetTop(L) - 1;
}

void RegisterInGame() {
    Game::Lua::RegisterGlobalFunction("xpcall", &Script_xpcall);
}

void RegisterGlue() {
    Game::Lua::RegisterGlueFunction("xpcall", &Script_xpcall);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::XPCall
