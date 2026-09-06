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

// `C_CVar.DoesCVarExist(name)` — a ClassicAPI addition, named for the
// `DoesAddOnExist` / `DoesSpellExist` pair. It answers the question callers
// actually have, which is whether GetCVar and SetCVar will work on this name,
// so it uses the same lookup they do. That makes the invariant exact:
//
//     C_CVar.DoesCVarExist(name) == (C_CVar.GetCVarInfo(name) ~= nil)
//
// and it holds for a name preserved out of Config.wtf without the client
// implementing it: such a cvar is real and reachable from the console, but no
// Lua getter can see it, so reporting it as existing would be a lie to every
// caller who then tried to read it.
//
// Returns false rather than raising for a non-string, matching the two
// Does*Exist functions it is named after -- a predicate should answer, not
// throw.
int __fastcall Script_C_CVar_DoesCVarExist(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    auto findCVar = reinterpret_cast<FindCVar_t>(Offsets::FUN_FIND_CVAR);
    Game::Lua::PushBool(L, findCVar(Game::Lua::ToString(L, 1)) != nullptr);
    return 1;
}

// `C_CVar.AreCVarsLoaded()` — whether the cvar registry is up.
//
// Reports the registry's own initialised state rather than a constant, so it
// states a fact instead of an assumption. In practice it is always true by the
// time anything can call it: the config system starts from the boot path
// (FUN_0063D380, reached straight out of the entry point), which registers
// commands and replays Config.wtf long before a Lua state exists. So there is
// no window in which Lua could observe false.
//
// It is a real question upstream, where an account's cvars can be stored
// server-side and arrive after login. Nothing here is fetched over the network,
// so once the boot path has run there is nothing further to wait for.
int __fastcall Script_C_CVar_AreCVarsLoaded(void *L) {
    const uint32_t mask = *reinterpret_cast<const uint32_t *>(Offsets::VAR_CVAR_HASH_MASK);
    Game::Lua::PushBool(L, mask != Offsets::CVAR_HASH_MASK_UNINITIALIZED);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarInfo", &Script_C_CVar_GetCVarInfo);
    Game::Lua::RegisterTableFunction("C_CVar", "DoesCVarExist",
                                     &Script_C_CVar_DoesCVarExist);
    Game::Lua::RegisterTableFunction("C_CVar", "AreCVarsLoaded",
                                     &Script_C_CVar_AreCVarsLoaded);
}

// Cvar storage is process-global, so the same surface works pre-login.
void RegisterGlueFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarInfo", &Script_C_CVar_GetCVarInfo);
    Game::Lua::RegisterTableFunction("C_CVar", "DoesCVarExist",
                                     &Script_C_CVar_DoesCVarExist);
    Game::Lua::RegisterTableFunction("C_CVar", "AreCVarsLoaded",
                                     &Script_C_CVar_AreCVarsLoaded);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlueFunctions};

} // namespace

} // namespace CVar::Info
