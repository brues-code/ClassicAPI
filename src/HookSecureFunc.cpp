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

#include "HookSecureFunc.h"

#include "Game.h"
#include "Offsets.h"

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

} // namespace

// Wrapper closure body — called when the hooked function is invoked.
// Stack contains the wrapper's args (`[arg1..argN]`); the closure's
// upvalues hold `(orig, callback)`. 4.3.4's FrameScript_HookWrapper, less
// its taint bookkeeping (1.12 has no taint):
//
//   1. orig(...) with lua_call — an error in the original propagates to
//      the caller, as it would unhooked.
//   2. Results sink below the args, so they end up as the wrapper's
//      return frame without being copied again.
//   3. callback(...) through lua_pcall with the engine's frame-script
//      message handler as errfunc: a broken hook is REPORTED the way any
//      script error is (the error frame / geterrorhandler), and the
//      original's results still reach the caller.
int __fastcall Wrapper(void *L) {
    const int nargs = Game::Lua::GetTop(L);

    // (1) Stack after: `[arg1..argN, res1..resM]`.
    for (int i = 1; i <= nargs; ++i)
        Game::Lua::PushValue(L, i);
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(1));
    Game::Lua::Insert(L, nargs + 1);
    Game::Lua::Call(L, nargs, Game::Lua::MULTRET);
    const int nresults = Game::Lua::GetTop(L) - nargs;

    // (2) Each `Insert(L, 1)` moves the top value to the bottom; nresults
    //     of them rotate the results block beneath the args.
    //     Stack after: `[res1..resM, arg1..argN]`.
    for (int i = 0; i < nresults; ++i)
        Game::Lua::Insert(L, 1);

    // (3) Stack: `[res1..resM, errfunc, callback, arg1..argN]`. The
    //     handler ref is the one every engine script pcall passes; 0 (not
    //     yet set) falls back to no handler rather than a bad index.
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(2));
    Game::Lua::Insert(L, nresults + 1);
    int errIdx = 0;
    const int errRef = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_FRAMESCRIPT_ERROR_HANDLER_REF));
    if (errRef > 0) {
        Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, errRef);
        Game::Lua::Insert(L, nresults + 1);
        errIdx = nresults + 1;
    }
    Game::Lua::PCall(L, nargs, 0, errIdx);

    // Drop the handler and any error value — `[res1..resM]` remains.
    Game::Lua::SetTop(L, nresults);
    return nresults;
}

void PushWrapper(void *L) {
    Game::Lua::PushCClosure(L, &Wrapper, 2);
}

namespace {

// `hooksecurefunc([table,] "function", hookfunc)` — replaces
// `table[name]` (default `_G`) with a wrapper that runs the original, then
// `hookfunc` with the same args, and returns the original's results.
// 4.3.4's Script_hooksecurefunc, less taint:
//   * a non-table first argument means `_G` is inserted at index 1, so
//     both forms share one index layout;
//   * the name only has to pass lua_isstring, so a number works (and is
//     looked up as its string, the same as lua_tostring makes it);
//   * the original is read with lua_gettable (inherited methods resolve
//     through __index) but the wrapper is written with lua_rawset, so a
//     target's __newindex never runs;
//   * the error texts are 4.3.4's.
// The one addition is the 11.0 unhookable-name check for the `_G` form.
int __fastcall Script_HookSecureFunc(void *L) {
    const bool implicitGlobals = Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE;
    if (implicitGlobals) {
        Game::Lua::PushValue(L, Game::Lua::GLOBALS_INDEX);
        Game::Lua::Insert(L, 1);
    }

    if (!Game::Lua::IsString(L, 2) ||
        Game::Lua::Type(L, 3) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "Usage: hooksecurefunc([table,] \"function\", hookfunc)");
        return 0;
    }
    Game::Lua::SetTop(L, 3);                    // [t, name, hookfunc]
    // Converts a number name to its string in place; the string stays on
    // the stack, so the pointer outlives every use below.
    const char *name = Game::Lua::ToString(L, 2);

    // Block the hook when targeting `_G` implicitly and the name is in the
    // unhookable set. Not applied to the three-arg form (table targets):
    // hooking `MyLib.pairs` is fine even if Lua's global `pairs` is not,
    // and a caller who passes `_G` explicitly is opting in deliberately.
    if (implicitGlobals && IsUnhookable(name)) {
        Game::Lua::Error(L, "hooksecurefunc: function is unhookable");
        return 0;
    }

    Game::Lua::PushValue(L, 2);
    Game::Lua::GetTable(L, 1);                  // [t, name, hookfunc, orig]
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "hooksecurefunc(): %s is not a function", name);
        return 0;
    }

    Game::Lua::Insert(L, 3);                    // [t, name, orig, hookfunc]
    PushWrapper(L);                             // [t, name, wrapper]
    Game::Lua::RawSet(L, 1);                    // t[name] = wrapper
    return 0;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("hooksecurefunc", &Script_HookSecureFunc);
}
// 4.3.4 registers it from FrameScript_Initialize, which runs for the glue
// state (Client.cpp) as well as the world (GameUI.cpp).
static void RegisterGlueFunctions() {
    Game::Lua::RegisterGlueFunction("hooksecurefunc", &Script_HookSecureFunc);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
static const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlueFunctions};

} // namespace HookSecureFunc
