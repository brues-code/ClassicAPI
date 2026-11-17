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
// purpose. Names with no 1.12 counterpart (the secure family does not
// exist) stay for parity of the error an addon sees. Sorted alphabetically.
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

// 1.12's taint is the context word: `lua_tainted` is
// VAR_FRAMESCRIPT_EXEC_CONTEXT, `lua_taintexpected` its depth, and each
// value carries a word at OFF_LUA_TVALUE_CONTEXT (see Offsets.h). A copied
// value with a nonzero word makes that word current while the depth is
// nonzero, and the protected-action gate (0x00494A50) refuses movement and
// similar actions under any nonzero context. 3.3.5's taint calls map 1:1;
// its `lua_taintedclosure` guard has no 1.12 counterpart (the engine's own
// script runner restores on the depth alone).
uint32_t &Context() {
    return Game::Ref<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
}

int32_t &Depth() {
    return Game::Ref<int32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH);
}

using IndexAcceptable_t = uint8_t *(__fastcall *)(void *L, int idx);

// `lua_settaint(L, idx, word)` — the value at a stack index takes `word`.
void SetContextWord(void *L, int idx, uint32_t word) {
    uint8_t *o = reinterpret_cast<IndexAcceptable_t>(Offsets::LUA_INDEX_ACCEPTABLE)(L, idx);
    *reinterpret_cast<uint32_t *>(o + Offsets::OFF_LUA_TVALUE_CONTEXT) = word;
}

// `if (lua_taintexpected) lua_tainted = word; if (--lua_taintexpected <= 0)
// lua_taintexpected = 0;` — the close of every 3.3.5 taint scope.
void CloseContextScope(uint32_t word) {
    if (Depth() != 0)
        Context() = word;
    if (--Depth() <= 0)
        Depth() = 0;
}

} // namespace

// Wrapper closure body — called when the hooked function is invoked.
// Stack contains the wrapper's args (`[arg1..argN]`); the closure's
// upvalues hold `(orig, callback)`. 3.3.5's FrameScript_HookWrapper
// (0x00817050):
//
//   1. orig(...) with lua_call under the caller's context — an error in
//      the original propagates to the caller, as it would unhooked, and a
//      protected function hooked from an addon still runs for a key
//      binding.
//   2. Results sink below the args, so they end up as the wrapper's
//      return frame without being copied again.
//   3. callback(...) through lua_pcall with the engine's frame-script
//      message handler as errfunc: a broken hook is REPORTED the way any
//      script error is (the error frame / geterrorhandler), and the
//      original's results still reach the caller. The callback's word
//      becomes current when it is pushed; the caller's context is put back
//      after, so the hook's taint does not leak into the caller.
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
    ++Depth();
    const uint32_t taint = Context();
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(2));
    Game::Lua::Insert(L, nresults + 1);
    int errIdx = 0;
    if (Game::Lua::PushErrorHandler(L) != 0) {
        Game::Lua::Insert(L, nresults + 1);
        errIdx = nresults + 1;
    }
    Game::Lua::PCall(L, nargs, 0, errIdx);
    CloseContextScope(taint);

    // Drop the handler and any error value — `[res1..resM]` remains.
    Game::Lua::SetTop(L, nresults);
    return nresults;
}

// 3.3.5's `lua_settaint` pair, `lua_pushcclosure`, `lua_settaint` on the
// closure. Copying the upvalues makes a nonzero word current, so the
// context is put back after, as 3.3.5's save/restore around it does.
void PushWrapper(void *L, uint32_t origWord, uint32_t hookWord) {
    const uint32_t saved = Context();
    SetContextWord(L, -2, origWord);
    SetContextWord(L, -1, hookWord);
    Game::Lua::PushCClosure(L, &Wrapper, 2);
    SetContextWord(L, -1, origWord);
    Context() = saved;
}

// 3.3.5's Script_HookScript (0x0049EDB0) from the script lookup on:
//   * no handler: the hook becomes the handler, stored under the caller's
//     context (3.3.5 `m_tainted = lua_tainted`);
//   * otherwise the wrapper over (old, hook) is stored under the SCRIPT's
//     context, which the slot keeps (3.3.5 leaves `m_tainted` alone), so a
//     client script hooked from an addon still runs as the client's.
// The caller's context is current again on return.
int SetHookedScript(void *L, uint32_t hookWord, uint32_t scriptWord,
                    int(__fastcall *setScript)(void *L)) {
    if (Game::Lua::Type(L, 4) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, 3);    // (self, type, hook)
        Context() = hookWord;
        return setScript(L);
    }
    Game::Lua::Insert(L, 3);        // (self, type, old, hook)
    PushWrapper(L, scriptWord, hookWord); // (self, type, wrapper)
    if (++Depth() != 0)
        Context() = scriptWord;
    const int n = setScript(L);
    CloseContextScope(hookWord);
    return n;
}

namespace {

// `hooksecurefunc([table,] "function", hookfunc)` — replaces
// `table[name]` (default `_G`) with a wrapper that runs the original, then
// `hookfunc` with the same args, and returns the original's results.
// 3.3.5's Script_hooksecurefunc (0x00817170):
//   * a non-table first argument means `_G` is inserted at index 1, so
//     both forms share one index layout;
//   * the name only has to pass lua_isstring, so a number works (and is
//     looked up as its string, the same as lua_tostring makes it);
//   * the original is read with lua_gettable (inherited methods resolve
//     through __index) but the wrapper is written with lua_rawset, so a
//     target's __newindex never runs;
//   * the wrapper keeps the ORIGINAL's context word, so hooking a client
//     function from an addon leaves it the client's: the JUMP binding
//     calling a hooked JumpOrAscendStart still passes the protected-action
//     gate. The original is read under a cleared context, so the context
//     afterwards is the original's own word (lua_gettable makes a nonzero
//     word current and stamps a zero one with the cleared context). The
//     hook keeps the caller's word. Both keys are pushed fresh rather than
//     copied, so the name's own word never becomes current;
//   * the error texts are 3.3.5's.
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

    const uint32_t taint = Context();
    if (++Depth() != 0)
        Context() = 0;

    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, 1);                  // [t, name, hookfunc, orig]
    const bool hooked = Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION;
    if (hooked) {
        const uint32_t functionTaint = Context();
        Game::Lua::Insert(L, 3);                // [t, name, orig, hookfunc]
        PushWrapper(L, functionTaint, taint);   // [t, name, wrapper]
        Game::Lua::PushString(L, name);
        Game::Lua::Insert(L, 3);                // [t, name, name, wrapper]
        Game::Lua::RawSet(L, 1);                // t[name] = wrapper
    }
    CloseContextScope(taint);

    if (!hooked) {
        Game::Lua::Error(L, "hooksecurefunc(): %s is not a function", name);
        return 0;
    }
    return 0;
}

} // namespace

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("table", "table", nullptr,
                   "The table that holds the function; the global table when omitted."),
    Game::Doc::Req("name", "string", "The key of the function to hook."),
    Game::Doc::Req("hookfunc", "function",
                   "Called after the original with the same arguments; its returns are "
                   "discarded."),
};
static const Game::Doc::Function kHookSecureFunc{
    "Hooks a function so another function runs after it, keeping the original's returns.",
    kArgs, {}, "FrameScriptGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("hooksecurefunc", &Script_HookSecureFunc,
                                      &kHookSecureFunc);
}
// 4.3.4 registers it from FrameScript_Initialize, which runs for the glue
// state (Client.cpp) as well as the world (GameUI.cpp).
static void RegisterGlueFunctions() {
    Game::Lua::RegisterGlueFunction("hooksecurefunc", &Script_HookSecureFunc, &kHookSecureFunc);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
static const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlueFunctions};

} // namespace HookSecureFunc
