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

// `GetCVar(name)` returns nil for an unknown cvar, as later clients do.
//
// Vanilla's Script_GetCVar raises "Couldn't find CVar named '<name>'" for a
// name its lookup does not know. Later clients return nil, and ported code
// relies on it as a feature probe: LibCamera-1.0 opens with
// `if not GetCVar("cameraYawE") then return end`, which on vanilla throws at
// file load instead of returning.
//
// Co-hook: when the argument is a string that FUN_FIND_CVAR - the same
// filtering lookup Script_GetCVar and C_CVar.DoesCVarExist use - does not
// find, push nil. Every other call, including a non-string argument, runs
// the engine function unchanged, so known cvars read exactly as before. The
// glue state registers the same function (cvar/Glue.cpp), so both states get
// the same behavior.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace CVar::Unknown {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using FindCVar_t = const uint8_t *(__fastcall *)(const char *name);

ScriptFn_t g_origGetCVar = nullptr;

int __fastcall GetCVar_h(void *L) {
    if (Game::Lua::IsString(L, 1)) {
        const char *name = Game::Lua::ToString(L, 1);
        if (name != nullptr &&
            reinterpret_cast<FindCVar_t>(Offsets::FUN_FIND_CVAR)(name) == nullptr) {
            Game::Lua::PushNil(L);
            return 1;
        }
    }
    return g_origGetCVar(L);
}

const Game::HookAutoRegister _getCVarHook{
    Offsets::FUN_SCRIPT_GET_CVAR,
    reinterpret_cast<void *>(&GetCVar_h),
    reinterpret_cast<void **>(&g_origGetCVar)};

} // namespace

} // namespace CVar::Unknown
