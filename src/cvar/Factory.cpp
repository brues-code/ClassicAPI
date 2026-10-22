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

#include "cvar/Factory.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdio>

namespace CVar::Factory {

namespace {

// `FUN_REGISTER_CVAR` — `__fastcall`, so MSVC puts name/help in ECX/EDX and
// pushes the rest right-to-left, which is the engine's own push order (see
// the boot registrations at FUN_00402BA0). `scriptHidden` 0 sets bit 31, the
// bit FUN_FIND_CVAR requires, so the cvar is reachable from Lua.
using RegisterCVar_t = void *(__fastcall *)(const char *name, const char *help,
                                            uint32_t flags,
                                            const char *defaultValue,
                                            ChangeCallback callback,
                                            int category, int scriptHidden,
                                            void *userData);

using FindCVar_t = void *(__fastcall *)(const char *name);

// `FUN_SET_CVAR_VALUE` — `__thiscall(cvar /*ecx*/, value, apply, setReset,
// setDefault, force)`: ECX = cvar with `value` AND the four flags all on the
// stack (5 stack args; the function cleans 20 bytes / `RET 0x14`). NOT
// __fastcall — that would route `value` through EDX, push only 4 args, and
// leave the callee popping 4 bytes too many → ESP corruption → silent crash.
// Mirror the Script_SetCVar invocation (0x00488CA8): 1, 0, 0, 1.
using SetCVarValue_t = char(__thiscall *)(void *cvar, const char *value,
                                          int apply, int setReset,
                                          int setDefault, int force);

} // namespace

Handle Register(const char *name, const char *help, int category,
                const char *defaultValue, ChangeCallback callback,
                void *userData) {
    if (name == nullptr)
        return nullptr;
    auto reg = reinterpret_cast<RegisterCVar_t>(Offsets::FUN_REGISTER_CVAR);
    return reg(name, help, /*flags*/ 0, defaultValue, callback, category,
               /*scriptHidden*/ 0, userData);
}

Handle Find(const char *name) {
    if (name == nullptr)
        return nullptr;
    auto find = reinterpret_cast<FindCVar_t>(Offsets::FUN_FIND_CVAR);
    return find(name);
}

const char *GetString(Handle cvar) {
    return cvar ? Game::Read<const char *>(cvar, Offsets::OFF_CVAR_VALUE_STR) : nullptr;
}

const char *GetDefaultString(Handle cvar) {
    return cvar ? Game::Read<const char *>(cvar, Offsets::OFF_CVAR_DEFAULT_STR) : nullptr;
}

int GetInt(Handle cvar, int fallback) {
    return cvar ? Game::Read<int>(cvar, Offsets::OFF_CVAR_VALUE_INT) : fallback;
}

float GetFloat(Handle cvar, float fallback) {
    return cvar ? Game::Read<float>(cvar, Offsets::OFF_CVAR_VALUE_FLOAT) : fallback;
}

bool IsReadOnly(Handle cvar) {
    return cvar != nullptr &&
           (Game::Read<uint32_t>(cvar, Offsets::OFF_CVAR_FLAGS) &
            Offsets::CVAR_FLAG_READ_ONLY) != 0;
}

void SetString(Handle cvar, const char *value, bool persist) {
    if (cvar == nullptr || value == nullptr)
        return;
    auto set = reinterpret_cast<SetCVarValue_t>(Offsets::FUN_SET_CVAR_VALUE);
    set(cvar, value, 1, 0, 0, persist ? 1 : 0);
}

void SetInt(Handle cvar, int value) {
    char buf[16]; // a 32-bit int, its sign and the terminator
    std::snprintf(buf, sizeof buf, "%d", value);
    SetString(cvar, buf);
}

} // namespace CVar::Factory
