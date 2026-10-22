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

#include "Factory.h"

#include "Game.h"

namespace CVar::Bool {

namespace {

// Case-insensitive "true" check for the string-form match. Bounded
// 4-char comparison + null terminator — same shape `BookTypeIsPet`
// uses in spell/Info.cpp.
bool IsTrueString(const char *s) {
    if (s == nullptr)
        return false;
    auto lc = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    };
    return lc(s[0]) == 't' && lc(s[1]) == 'r' && lc(s[2]) == 'u' &&
           lc(s[3]) == 'e' && s[4] == '\0';
}

// Coerces a cvar's string value to bool per modern semantics:
//   - empty / nil  → false
//   - "0"           → false
//   - "1" or any numeric non-zero → true
//   - "true" (case-insensitive) → true
//   - anything else (non-numeric, non-"true") → false
bool StringToBool(const char *s) {
    if (s == nullptr || s[0] == '\0')
        return false;
    if (IsTrueString(s))
        return true;
    // Parse leading integer; non-zero means true. atoi-style.
    bool negative = false;
    int idx = 0;
    if (s[idx] == '-') { negative = true; ++idx; }
    else if (s[idx] == '+') { ++idx; }
    if (s[idx] < '0' || s[idx] > '9')
        return false;
    int value = 0;
    while (s[idx] >= '0' && s[idx] <= '9') {
        value = value * 10 + (s[idx] - '0');
        ++idx;
    }
    (void)negative; // sign doesn't matter — non-zero is non-zero
    return value != 0;
}

// `C_CVar.GetCVarBool(cvar)` — looks the cvar up directly through
// the engine's by-name hash table at `FUN_FIND_CVAR`, reads the
// value string at `+0x20`, coerces to bool. Returns `nil` for
// unknown cvars so callers can distinguish "missing" from "set to
// false" — same shape as modern `C_CVar.GetCVarBool`.
//
// Going through `Script_GetCVar` would work too but adds a Lua-stack
// roundtrip and raises a Lua error for unknown cvars
// (`"CVar \"%s\" doesn't exist."`). The direct lookup lets us turn
// that into the nil return cleanly.
int __fastcall Script_C_CVar_GetCVarBool(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_CVar.GetCVarBool(\"cvar\")");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);

    CVar::Factory::Handle cvar = CVar::Factory::Find(name);
    if (cvar == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushBool(L, StringToBool(CVar::Factory::GetString(cvar)));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarBool",
                                      &Script_C_CVar_GetCVarBool);
}

// Same surface on the glue Lua state — CVar storage is process-global
// so the polyfill works identically pre-login. `RegisterTableFunction`
// reads `VAR_LUA_STATE` to pick which state it writes to, which during
// the glue init hook points at the glue state.
void RegisterGlueFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarBool",
                                      &Script_C_CVar_GetCVarBool);
}

const Game::ModuleAutoRegister     _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlueFunctions};

} // namespace

} // namespace CVar::Bool
