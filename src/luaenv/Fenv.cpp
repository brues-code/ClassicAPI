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

// getfenv / setfenv `__environment` protection — Lua 5.1 backport.
//
// A sandbox hands restricted code an environment table E and puts a metatable
// on it carrying `__environment = <safe value>`. From then on, code inside the
// sandbox that calls `getfenv()` gets the safe value instead of E (so it can't
// reach or mutate the real environment), and `setfenv` refuses to change E.
//
// 1.12's engine ALREADY implements this protection — its `getfenv`
// (0x00702AC0) and `setfenv` (0x00702BF8) share one predicate (0x00702BA0)
// that reads a marker off the environment and, when present, substitutes it
// (getfenv) or errors "cannot change a protected environment" (setfenv). But
// 1.12 reads a RAW `__fenv` field on the env TABLE, whereas Lua 5.1 (verified
// against 3.3.5's `FUN_0084F2F0`) reads a RAW `__environment` field off the
// env's METATABLE. Modern addons written for 5.1 use the metatable form.
//
// We co-hook the ONE shared predicate and REPLACE it (the original is never
// called) so both getfenv and setfenv gain the 5.1 semantics at once. The
// reimplementation preserves the predicate's exact stack contract — push the
// function's environment, then push the marker value (or nil), net +2, and
// return whether the marker is non-nil — which getfenv (`settop -2`) and
// setfenv (`settop -3`, then error) both depend on.
//
// Order: metatable `__environment` (5.1) is checked first; the raw `__fenv`
// field (1.12's original) is kept as a fallback, so anything still relying on
// `__fenv` — e.g. compiled FrameXML we can't see on disk — keeps working. The
// change is additive: nothing on disk sets either marker, so for every
// existing addon the predicate returns nil and getfenv/setfenv behave exactly
// as before. It only activates when code deliberately marks an environment.

#include "Game.h"
#include "Offsets.h"

namespace LuaEnv {

namespace {

// Raw engine primitives not exposed via Game::Lua.
using GetFenv_t = void(__fastcall *)(void *L, int idx);
using GetMetatable_t = int(__fastcall *)(void *L, int idx);
const auto GetFenv = reinterpret_cast<GetFenv_t>(Offsets::LUA_GET_FENV);
const auto GetMetatable = reinterpret_cast<GetMetatable_t>(Offsets::LUA_GET_METATABLE);

// Replacement for the shared getfenv/setfenv protection predicate
// (FUN_LUA_ENV_PROTECT_PREDICATE). Contract mirrored exactly from the engine
// original: the function whose environment to test is at stack top (-1); on
// return the stack has grown by 2 (env, marker) and eax is non-zero iff the
// environment is protected.
int __fastcall EnvProtectPredicate_h(void *L) {
    GetFenv(L, -1); // push the function's environment -> [.., env]

    bool found = false;
    if (GetMetatable(L, -1) != 0) { // -> [.., env, metatable]
        Game::Lua::PushLString(L, "__environment", 13);
        Game::Lua::RawGet(L, -2); // raw metatable["__environment"] -> [.., env, mt, marker]
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL) {
            Game::Lua::Remove(L, -2); // drop the metatable -> [.., env, marker]
            found = true;
        } else {
            Game::Lua::SetTop(L, -3); // drop the nil + metatable -> [.., env]
        }
    }
    if (!found) {
        // Fallback: 1.12's original raw `__fenv` field on the env table.
        Game::Lua::PushLString(L, "__fenv", 6);
        Game::Lua::RawGet(L, -2); // raw env["__fenv"] -> [.., env, marker|nil]
    }
    return Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL;
}

// Full replacement — the original predicate is never invoked (nullptr trampoline).
const Game::HookAutoRegister _hook{Offsets::FUN_LUA_ENV_PROTECT_PREDICATE,
                                   reinterpret_cast<void *>(&EnvProtectPredicate_h),
                                   nullptr};

} // namespace

} // namespace LuaEnv
