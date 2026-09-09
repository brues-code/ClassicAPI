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

#include "Offsets.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace CVar::Factory {

namespace {

// `FUN_REGISTER_CVAR` — verified ABI (see Offsets.h and the calibration
// against the engine's own call site at 0x005E2490 / the Lua
// Script_RegisterCVar path). MSVC `__fastcall` places the first two args in
// ECX/EDX and pushes the rest right-to-left, which reproduces the engine's
// exact push order: flags, defaultValue, callback, categoryId, hiddenBool,
// userData (with flags as the first stack arg / last push).
using RegisterCVar_t = void *(__fastcall *)(const char *name, int edxZero,
                                            uint32_t flags,
                                            const char *defaultValue,
                                            ChangeCallback callback,
                                            int categoryId, int hiddenBool,
                                            void *userData);

using FindCVar_t = void *(__fastcall *)(const char *name);

// `FUN_SET_CVAR_VALUE` — `__thiscall(cvar /*ecx*/, value, a3, a4, a5, a6)`,
// i.e. ECX=cvar with `value` AND a3..a6 all passed on the stack (5 stack
// args; the function cleans 20 bytes / `RET 0x14`). NOT __fastcall — that
// would route `value` through EDX, push only 4 args, and leave the callee
// popping 4 bytes too many → ESP corruption → silent crash. Mirror the
// Script_SetCVar invocation (0x00488CA8): a3..a6 = 1, 0, 0, 1.
using SetCVarValue_t = char(__thiscall *)(void *cvar, const char *value,
                                          int a3, int a4, int a5, int a6);

const char *ReadStringField(void *cvar, uintptr_t offset) {
    if (cvar == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(
        static_cast<const uint8_t *>(cvar) + offset);
}

const char *ReadValue(void *cvar) {
    return ReadStringField(cvar, Offsets::OFF_CVAR_VALUE_STR);
}

} // namespace

Handle Register(const char *name, const char *defaultValue, uint32_t flags,
                ChangeCallback callback, void *userData) {
    if (name == nullptr)
        return nullptr;
    auto reg = reinterpret_cast<RegisterCVar_t>(Offsets::FUN_REGISTER_CVAR);
    return reg(name, /*edxZero*/ 0, flags, defaultValue, callback,
               Offsets::CVAR_SCRIPT_CATEGORY, /*hiddenBool*/ 0, userData);
}

Handle Find(const char *name) {
    if (name == nullptr)
        return nullptr;
    auto find = reinterpret_cast<FindCVar_t>(Offsets::FUN_FIND_CVAR);
    return find(name);
}

const char *GetString(Handle cvar) {
    return ReadValue(cvar);
}

const char *GetDefaultString(Handle cvar) {
    return ReadStringField(cvar, Offsets::OFF_CVAR_DEFAULT_STR);
}

int GetInt(Handle cvar, int fallback) {
    const char *value = ReadValue(cvar);
    if (value == nullptr || value[0] == '\0')
        return fallback;
    return std::atoi(value);
}

void SetString(Handle cvar, const char *value) {
    if (cvar == nullptr || value == nullptr)
        return;
    auto set = reinterpret_cast<SetCVarValue_t>(Offsets::FUN_SET_CVAR_VALUE);
    set(cvar, value, 1, 0, 0, 1);
}

void SetInt(Handle cvar, int value) {
    char buf[16];
    // Hand-format to avoid pulling in a locale-aware path; value range for
    // our cvars is small, 16 bytes is ample for a 32-bit int + sign + NUL.
    std::snprintf(buf, sizeof buf, "%d", value);
    SetString(cvar, buf);
}

} // namespace CVar::Factory
