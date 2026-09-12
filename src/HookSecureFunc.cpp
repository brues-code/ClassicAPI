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

#include <cstring>

namespace HookSecureFunc {

namespace {

// Names that must not be hooked when targeting `_G`. This is the client's
// own list, VERBATIM: patch 11.0.0 (The War Within, 2024-07-23) made
// `hooksecurefunc` refuse exactly these 23 names with a "Cannot hook
// function" error — the taint-relevant primitives (the "secure" family plus
// the Lua primitives secure code paths run through). It is deliberately NOT
// "every base-library function": 11.0 leaves `tostring`, `error`,
// `loadstring`, `assert`, `collectgarbage`, … hookable, and includes names
// that are not base-library functions at all. So do not derive this from
// the engine's `luaopen_base` table (`0x00811E28`, 36 entries — see
// BlizzardScriptAPI.md §3); that is a different set with a different
// purpose. Names with no 1.12 counterpart (`wipe` is `table.wipe` here; the
// secure family does not exist) stay for parity of the error an addon
// sees. Sorted alphabetically.
const char *const kUnhookableNames[] = {
    "getfenv", "getmetatable", "hooksecurefunc", "ipairs", "issecurevalue",
    "issecurevariable", "next", "pairs", "pcall", "pcallwithenv", "rawget",
    "rawset", "scrub", "securecall", "securecallfunction",
    "secureexecuterange", "select", "setfenv", "setmetatable", "type",
    "unpack", "wipe", "xpcall",
};

bool IsUnhookable(const char *name) {
    if (name == nullptr)
        return false;
    for (const char *blocked : kUnhookableNames) {
        if (std::strcmp(name, blocked) == 0)
            return true;
    }
    return false;
}

// Wrapper closure body — called when the hooked function is invoked.
// Stack contains the wrapper's args (`[arg1..argN]`); the closure's
// upvalues hold `(orig, callback)`.
//
// Modern `hooksecurefunc` semantics: original runs first, callback
// runs after with the same args (returns ignored), original's
// results propagate to the caller. No cap on return count. A buggy
// callback must NOT prevent the original's results from reaching
// the caller — that's why step (3) uses pcall.
//
// Stack-shuffling mirrors 3.3.5's `FUN_00817050` so the args sit at
// the top for both calls without copying them twice; results sink
// to the bottom and end up as the wrapper's natural return frame.
int __fastcall HookWrapper(void *L) {
    const int nargs = Game::Lua::GetTop(L);

    // (1) orig(...) — pops orig + nargs args (copies), pushes results.
    //     Stack after: `[arg1..argN, res1..resM]`.
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(1));
    for (int i = 1; i <= nargs; ++i)
        Game::Lua::PushValue(L, i);
    Game::Lua::Call(L, nargs, Game::Lua::MULTRET);
    const int nresults = Game::Lua::GetTop(L) - nargs;

    // (2) Shuffle results below the saved args. Each `Insert(L, 1)`
    //     moves the current top down to position 1; running it
    //     nresults times rotates the results block beneath the args.
    //     Stack after: `[res1..resM, arg1..argN]`.
    for (int i = 0; i < nresults; ++i)
        Game::Lua::Insert(L, 1);

    // (3) callback(...) via pcall — errors are caught and dropped so
    //     the original's return values always reach the caller. This
    //     is the load-bearing safety property of hooksecurefunc on
    //     3.x; on 1.12 it just means a buggy hook can't break the
    //     hooked path. errfunc=0 → no traceback (3.3.5 uses
    //     `registry["_TRACEBACK"]`, not a 1.12 convention; we'd be
    //     discarding the formatted message anyway).
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(2));
    Game::Lua::Insert(L, nresults + 1);
    if (Game::Lua::PCall(L, nargs, 0, 0) != 0) {
        // pcall failure leaves the error message on top; drop it.
        Game::Lua::Remove(L, -1);
    }

    // Stack: `[res1..resM]` — return the original's results verbatim.
    return nresults;
}

// `hooksecurefunc(name, callback)` — hooks a global by name, target
// table is `_G`.
// `hooksecurefunc(table, name, callback)` — hooks `table[name]`.
//
// In both cases the original function runs first and its results
// propagate; `callback` runs after with the same args (returns
// discarded). The "secure" label refers to taint propagation in
// 3.x+; on 1.12 it's just a post-hook with return preservation.
int __fastcall Script_HookSecureFunc(void *L) {
    int targetIdx, nameIdx, callbackIdx;
    const int t1 = Game::Lua::Type(L, 1);

    if (t1 == Game::Lua::TYPE_TABLE) {
        // Three-arg form: (table, name, callback)
        targetIdx = 1;
        nameIdx = 2;
        callbackIdx = 3;
    } else if (t1 == Game::Lua::TYPE_STRING) {
        // Two-arg form: (name, callback) — target = _G via pseudo-
        // index. Don't push _G with `lua_pushvalue(GLOBALS_INDEX)`:
        // it crashes (the pushvalue → setobj2s chain dereferences a
        // bad TValue pointer for that pseudo-index). `lua_gettable` /
        // `lua_settable` accept GLOBALS_INDEX as the table arg
        // directly, so we use it that way without pushing.
        targetIdx = Game::Lua::GLOBALS_INDEX;
        nameIdx = 1;
        callbackIdx = 2;
    } else {
        Game::Lua::Error(L,
            "hooksecurefunc(table, name, callback) or hooksecurefunc(name, callback)");
        return 0;
    }

    if (Game::Lua::Type(L, nameIdx) != Game::Lua::TYPE_STRING) {
        Game::Lua::Error(L, "hooksecurefunc: name must be a string");
        return 0;
    }
    if (Game::Lua::Type(L, callbackIdx) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "hooksecurefunc: callback must be a function");
        return 0;
    }

    // Block the hook when targeting `_G` and the name is in the unhookable
    // set. We don't apply this to the three-arg form (table targets):
    // hooking `MyLib.pairs` is fine even if Lua's global `pairs` is not —
    // the blacklist is specifically about `_G[name]`. A user who passes
    // `_G` explicitly as the table target is opting in deliberately and we
    // don't second-guess it.
    if (targetIdx == Game::Lua::GLOBALS_INDEX) {
        const char *name = Game::Lua::ToString(L, nameIdx);
        if (IsUnhookable(name)) {
            Game::Lua::Error(L,
                "hooksecurefunc: function is unhookable");
            return 0;
        }
    }

    // Fetch target[name] — the original function. `gettable` uses
    // metamethods, so frame methods (resolved via the frame
    // metatable's __index) come back as callable closures, exactly
    // what we want to wrap.
    Game::Lua::PushValue(L, nameIdx);
    Game::Lua::GetTable(L, targetIdx);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "hooksecurefunc: target field is not a function");
        return 0;
    }

    // Push callback as the second upvalue; orig is already on top
    // from the GetTable above.
    Game::Lua::PushValue(L, callbackIdx);

    // Build the wrapper as a C closure with (orig, callback) as the
    // two upvalues. PushCClosure pops the upvalues from the stack
    // and pushes the closure.
    Game::Lua::PushCClosure(L, &HookWrapper, 2);

    // Install: target[name] = wrapper. SetTable expects the value
    // on top and the key just below; push them in that order.
    Game::Lua::PushValue(L, nameIdx);
    Game::Lua::PushValue(L, -2);  // duplicate the closure
    Game::Lua::SetTable(L, targetIdx);

    return 0;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("hooksecurefunc", &Script_HookSecureFunc);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace HookSecureFunc
