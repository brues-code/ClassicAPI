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

// `C_CVar.GetCVarInfo(name)` — a cvar's value, its default, and what may be
// done to it.
//
//   value, defaultValue, isStoredServerAccount, isStoredServerCharacter,
//   isLockedFromUser, isSecure, isReadOnly
//
// Two of the seven are real here and five are structurally false, which is
// worth stating precisely rather than leaving as "not implemented":
//
//   isReadOnly              REAL. Flag bit 0x4. Script_SetCVar tests it before
//                           anything else and raises `"%s" is read only`, so a
//                           caller checking this gets exactly the answer that
//                           decides whether SetCVar will work.
//   isStoredServerAccount   Always false. These describe a cvar the server
//   isStoredServerCharacter stores per account or per character, synchronised
//                           through the `synchronizeConfig` cvar. That whole
//                           mechanism postdates this client — there is no
//                           server cvar sync and no flag bit for one, so false
//                           is the accurate answer, not a placeholder.
//   isLockedFromUser        Always false. No flag governs it.
//   isSecure                Always false. Marks cvars that cannot be set while
//                           in combat, a protection this client has no notion
//                           of.
//
// Derived by reading 3.3.5's own GetCVarInfo, which returns the first four of
// these: value and default from the cvar struct, then flag bits 4 and 5 for the
// two server-stored booleans. The FLAG OFFSET transfers — +0x1C in both — but
// the string offsets do not (3.3.5 keeps them at +0x28 and +0x40), so the ones
// used here come from this client's own `cvarlist` handler instead.
//
// Unknown names return nil rather than raising, so a caller can tell "no such
// cvar" from "set to empty". FUN_FIND_CVAR searches the cvar registry alone, so
// a console COMMAND's name finds nothing and returns nil — which is the
// contract's "only accepts console variables" without needing a test for it.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace CVar::Info {

namespace {

using FindCVar_t = const uint8_t *(__fastcall *)(const char *name);

int __fastcall Script_C_CVar_GetCVarInfo(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_CVar.GetCVarInfo(\"cvar\")");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);

    auto findCVar = reinterpret_cast<FindCVar_t>(Offsets::FUN_FIND_CVAR);
    const uint8_t *cvar = findCVar(name);
    if (cvar == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const char *value =
        *reinterpret_cast<const char *const *>(cvar + Offsets::OFF_CVAR_VALUE_STR);
    const char *defaultValue =
        *reinterpret_cast<const char *const *>(cvar + Offsets::OFF_CVAR_DEFAULT_STR);
    const uint32_t flags =
        *reinterpret_cast<const uint32_t *>(cvar + Offsets::OFF_CVAR_FLAGS);

    // A cvar registered with no default leaves the pointer null; report the
    // empty string so every return keeps its declared type.
    Game::Lua::PushString(L, value != nullptr ? value : "");
    Game::Lua::PushString(L, defaultValue != nullptr ? defaultValue : "");
    Game::Lua::PushBool(L, false); // isStoredServerAccount
    Game::Lua::PushBool(L, false); // isStoredServerCharacter
    Game::Lua::PushBool(L, false); // isLockedFromUser
    Game::Lua::PushBool(L, false); // isSecure
    Game::Lua::PushBool(L, (flags & Offsets::CVAR_FLAG_READ_ONLY) != 0);
    return 7;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarInfo", &Script_C_CVar_GetCVarInfo);
}

// Cvar storage is process-global, so the same surface works pre-login.
void RegisterGlueFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarInfo", &Script_C_CVar_GetCVarInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlueFunctions};

} // namespace

} // namespace CVar::Info
