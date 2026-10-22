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

// getfenv / setfenv — Lua 5.1 backport (4.3.4 / 3.3.5 `lbaselib.c`).
//
// 1.12 ships stock Lua 5.0 semantics: a RAW `__fenv` field on the env table
// both substitutes getfenv's result and makes setfenv error "cannot change a
// protected environment", and setfenv returns nothing. The modern client
// differs in three ways, all mirrored here:
//
//   * getfenv returns `getmetatable(env).__environment` (raw) when set, else
//     the env — the `luaL_getmetafield` read.
//   * setfenv has NO protection check; `__environment` only hides the env
//     from getfenv.
//   * setfenv returns the function it changed, so
//     `local f = setfenv(loadstring(src), env)` works.
//
// getfenv's own body (getfunc → predicate → `settop -2` when unprotected) is
// already the 5.1 shape, so we only replace the predicate it calls. setfenv
// is replaced whole; it still calls the engine's getfunc for arg resolution
// and level errors (the tail-call error is already 1.12's).

#include "Game.h"
#include "Offsets.h"

namespace LuaEnv {

namespace {

// Raw engine primitives not exposed via Game::Lua.
using GetFenv_t = void(__fastcall *)(void *L, int idx);
using SetFenv_t = int(__fastcall *)(void *L, int idx);
using Replace_t = void(__fastcall *)(void *L, int idx);
using CheckType_t = void(__fastcall *)(void *L, int narg, int t);
using GetFunc_t = void(__fastcall *)(void *L);
const auto GetFenv = reinterpret_cast<GetFenv_t>(Offsets::LUA_GET_FENV);
const auto SetFenv = reinterpret_cast<SetFenv_t>(Offsets::LUA_SET_FENV);
const auto Replace = reinterpret_cast<Replace_t>(Offsets::LUA_REPLACE);
const auto CheckType = reinterpret_cast<CheckType_t>(Offsets::LUAL_CHECK_TYPE);
const auto GetFunc = reinterpret_cast<GetFunc_t>(Offsets::FUN_LUA_BASE_GETFUNC);

using Game::Lua::GetMetatable;

// Replacement for the protection predicate, now called only by getfenv
// (FUN_SCRIPT_SETFENV no longer reaches it). Contract mirrored from the
// engine original: the function whose environment to test is at stack top
// (-1); on return the stack has grown by 2 (env, marker) and eax is non-zero
// iff the marker is set — getfenv then drops the marker (`settop -2`) when
// it is not, leaving the env.
int __fastcall EnvProtectPredicate_h(void *L) {
    GetFenv(L, -1); // push the function's environment -> [.., env]
    if (GetMetatable(L, -1) == 0) { // -> [.., env, metatable]
        Game::Lua::PushNil(L); // no metatable -> [.., env, nil]
        return false;
    }
    Game::Lua::PushLString(L, "__environment", 13);
    Game::Lua::RawGet(L, -2);  // raw metatable["__environment"] -> [.., env, mt, marker]
    Game::Lua::Remove(L, -2);  // drop the metatable -> [.., env, marker]
    return Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL;
}

// luaB_setfenv, 5.1.
int __fastcall Script_SetFenv(void *L) {
    CheckType(L, 2, Game::Lua::TYPE_TABLE);
    GetFunc(L);                    // -> [.., f]
    Game::Lua::PushValue(L, 2);    // -> [.., f, t]
    if (Game::Lua::IsNumber(L, 1) && Game::Lua::ToNumber(L, 1) == 0) {
        // Change the thread's environment — 5.0's globals slot.
        Replace(L, Game::Lua::GLOBALS_INDEX);
        return 0;
    }
    // lua_setfenv pops t, and returns 0 for C functions as 5.1's
    // `lua_iscfunction` test does.
    if (SetFenv(L, -2) == 0)
        Game::Lua::Error(L, "'setfenv' cannot change environment of given object");
    return 1; // f
}

// Full replacements — the originals are never invoked (nullptr trampolines).
const Game::HookAutoRegister _predicateHook{Offsets::FUN_LUA_ENV_PROTECT_PREDICATE,
                                            reinterpret_cast<void *>(&EnvProtectPredicate_h),
                                            nullptr};
const Game::HookAutoRegister _setfenvHook{Offsets::FUN_SCRIPT_SETFENV,
                                          reinterpret_cast<void *>(&Script_SetFenv), nullptr};

} // namespace

} // namespace LuaEnv
