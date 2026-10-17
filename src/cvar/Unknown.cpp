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

// `GetCVar(name)` / `GetCVarDefault(name)` stop raising for an unknown cvar,
// as later clients do.
//
// Vanilla's Script_GetCVar (0x00488BA0) and Script_GetCVarDefault
// (0x00488CF0) share one shape: lua_isstring(1) -> lua_tostring(1) ->
// FUN_FIND_CVAR -> on NULL raise "Couldn't find CVar named '<name>'". Later
// clients return instead, and ported code relies on it as a feature probe:
// LibCamera-1.0 opens with `if not GetCVar("cameraYawE") then return end`,
// which on vanilla throws at file load instead of returning.
//
// Both return NO values, not nil - 1.15.9 `/dump GetCVar("asd")` and
// `/dump C_CVar.GetCVarDefault("asd")` each print "empty result" - so the
// hooks return 0.
//
// Co-hooks: when the argument is a string that FUN_FIND_CVAR - the same
// filtering lookup both engine functions and C_CVar.DoesCVarExist use - does
// not find, skip the engine function. Every other call, including a
// non-string argument, runs the engine function unchanged, so known cvars
// read exactly as before. The glue state registers the same functions
// (cvar/Glue.cpp), so both states get the same behavior.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace CVar::Unknown {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using FindCVar_t = const uint8_t *(__fastcall *)(const char *name);

ScriptFn_t g_origGetCVar = nullptr;
ScriptFn_t g_origGetCVarDefault = nullptr;

// True when argument 1 is a string naming no cvar Lua can see - the exact
// case the engine functions raise for.
bool IsUnknownName(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return false;
    const char *name = Game::Lua::ToString(L, 1);
    return name != nullptr &&
           reinterpret_cast<FindCVar_t>(Offsets::FUN_FIND_CVAR)(name) == nullptr;
}

int __fastcall GetCVar_h(void *L) {
    if (IsUnknownName(L))
        return 0;
    return g_origGetCVar(L);
}

int __fastcall GetCVarDefault_h(void *L) {
    if (IsUnknownName(L))
        return 0;
    return g_origGetCVarDefault(L);
}

const Game::HookAutoRegister _getCVarHook{
    Offsets::FUN_SCRIPT_GET_CVAR,
    reinterpret_cast<void *>(&GetCVar_h),
    reinterpret_cast<void **>(&g_origGetCVar)};

const Game::HookAutoRegister _getCVarDefaultHook{
    Offsets::FUN_SCRIPT_GET_CVAR_DEFAULT,
    reinterpret_cast<void *>(&GetCVarDefault_h),
    reinterpret_cast<void **>(&g_origGetCVarDefault)};

} // namespace

} // namespace CVar::Unknown
