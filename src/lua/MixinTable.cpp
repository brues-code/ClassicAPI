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

#include "lua/MixinTable.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Mixin.h"
#include "lua/Arg.h"

#include <cstdio>

namespace Game::Lua::MixinTable {

namespace {

// Pushes the environment FindMixin reads the mixin from: the first Lua
// function's, walking the stack from level 0 (our C function), else the
// globals. lua_getinfo "f" pushes nil for a lost tail call, which is skipped.
void PushCallerEnvironment(void *L) {
    alignas(4) uint8_t ar[Offsets::SIZEOF_LUA_DEBUG];
    for (int level = 0; GetStack(L, level, ar); ++level) {
        GetInfo(L, "f", ar);
        if (Type(L, -1) == TYPE_FUNCTION && !IsCFunction(L, -1)) {
            GetFenv(L, -1);
            Remove(L, -2);
            return;
        }
        SetTop(L, -2);
    }
    PushValue(L, GLOBALS_INDEX);
}

} // namespace

void Push(void *L, const char *name) {
    PushCallerEnvironment(L);
    PushString(L, name);
    RawGet(L, -2);
    Remove(L, -2);
    if (Type(L, -1) != TYPE_TABLE) {
        // The generated wrappers run the body under pcall and raise its error
        // again from a char[200], so the text has no position.
        char text[Arg::kErrorCap + 1];
        std::snprintf(text, sizeof text, "unable to find mixin or metatable (%s)", name);
        Arg::RaiseMessage(L, text);
        return;
    }
    const int mixin = GetTop(L);
    NewTable(L);
    BaseLib::Mixin::Into(L, mixin + 1, mixin);
    Remove(L, mixin);
}

void PushColor(void *L, uint32_t argb) {
    const auto channel = [argb](int shift) {
        return static_cast<float>((argb >> shift) & 0xFF) * (1.0f / 255.0f);
    };
    Push(L, "ColorMixin");
    SetFieldNumber(L, "r", channel(16));
    SetFieldNumber(L, "g", channel(8));
    SetFieldNumber(L, "b", channel(0));
    SetFieldNumber(L, "a", channel(24));
}

} // namespace Game::Lua::MixinTable
