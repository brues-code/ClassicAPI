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

// `C_CVar.SetCVar(name [, value]) -> success` — the modern setter, which
// reports the result where vanilla's global SetCVar returns nothing.
//
// Retail 12.x probe (SoftTargetFriend):
//   - value 2: true, "2"; no value: true, "0"; value 1.5: true, "1.5";
//   - value true: error "bad argument #2 to 'SetCVar' (Usage: local success =
//     C_CVar.SetCVar(name [, value]))"; no arguments: the same on #1;
//   - pcall with "NoSuchCVarXyz" or 5 as the name: true and no return.
//
// So a string or number value is taken as its string, a missing or nil one
// is "0" (4.3.4's Script_SetCVar default too), and any other type is the
// argument error. An unknown name returns nothing. The lookup is
// FUN_FIND_CVAR, the one every Lua cvar function uses, so a cvar Lua cannot
// see is unknown here as well. A read-only cvar returns false: the engine
// setter silently ignores it (FUN_0063E0B0 opens with `if (flags & 4)
// return;`). No 1.12 cvar is secure, so the documented nil for a secure cvar
// in combat does not occur.
//
// The write is Script_SetCVar's (FUN_00488C10): the engine setter with
// 1, 0, 0, 1, so the change callback runs and Config.wtf saves the value. That
// setter returns 1 whatever its callback decides, so success is true once the
// cvar is writable, as it is in retail.

#include "Factory.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace CVar::Set {

namespace {

constexpr const char *kUsage = "Usage: local success = C_CVar.SetCVar(name [, value])";

int __fastcall Script_SetCVar(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::ArgError(L, 1, kUsage);
        return 0;
    }
    const char *value = "0";
    if (Game::Lua::Type(L, 2) > Game::Lua::TYPE_NIL) {
        if (!Game::Lua::IsString(L, 2)) {
            Game::Lua::ArgError(L, 2, kUsage);
            return 0;
        }
        value = Game::Lua::ToString(L, 2);
    }
    CVar::Factory::Handle cvar = CVar::Factory::Find(Game::Lua::ToString(L, 1));
    if (cvar == nullptr)
        return 0;
    const uint32_t flags = *reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(cvar) + Offsets::OFF_CVAR_FLAGS);
    if ((flags & Offsets::CVAR_FLAG_READ_ONLY) != 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    CVar::Factory::SetString(cvar, value);
    Game::Lua::PushBool(L, true);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("name", "cstring", "The name of the cvar."),
    Game::Doc::Opt("value", "cstring", "\"0\"", "The new value. A number is set as its text."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("success", "bool",
                   "True when the cvar was set, false when it is read-only. Nothing for an "
                   "unknown cvar."),
};
const Game::Doc::Function kDoc{"Sets a console variable and saves it.", kArgs, kRets};

void Register() {
    Game::Lua::RegisterTableFunction("C_CVar", "SetCVar", &Script_SetCVar, &kDoc);
}

// Cvar storage is process-global, so the same surface works pre-login.
const Game::ModuleAutoRegister _autoreg{&Register};
const Game::GlueModuleAutoRegister _glueAutoreg{&Register};

} // namespace

} // namespace CVar::Set
