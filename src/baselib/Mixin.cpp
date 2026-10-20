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

// `Mixin(object, ...)` and `CreateFromMixins(...)` — the FrameXML table-mixin
// primitives. Modern WoW provides these as engine C functions (in TableUtil);
// we do the same rather than leaving them in the !!!ClassicAPI addon so they
// exist whenever the DLL is injected, even if the addon is disabled. The
// composite `CreateAndInitFromMixin` (which calls a Lua `:Init` method) stays
// Lua-side in the addon, mirroring retail's Mixin.lua.
//
// Behaviour matches the canonical Lua implementation exactly:
//   Mixin(object, ...)      -- shallow-copy every key/value from each table
//                              argument onto `object`; returns `object`.
//   CreateFromMixins(...)   -- Mixin({}, ...); returns the new table.
// Non-table extra arguments are skipped (the Lua form guards nil the same way).

#include "baselib/Mixin.h"

#include "Game.h"

namespace BaseLib::Mixin {

// Shallow-copy every pair from the table at `srcIdx` onto the table at
// `dstIdx` (both absolute, positive stack indices). Stack-balanced.
void Into(void *L, int dstIdx, int srcIdx) {
    Game::Lua::PushNil(L);
    while (Game::Lua::Next(L, srcIdx) != 0) {
        // stack top: ..., key (-2), value (-1). Copy both so the originals
        // survive for the next Next() iteration.
        Game::Lua::PushValue(L, -2); // key copy
        Game::Lua::PushValue(L, -2); // value copy
        Game::Lua::SetTable(L, dstIdx); // dst[key] = value; pops the two copies
        Game::Lua::SetTop(L, -2);       // pop value, leave key for Next()
    }
}

namespace {

int __fastcall Script_Mixin(void *L) {
    const int top = Game::Lua::GetTop(L);
    if (top < 1 || Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: Mixin(object, ...)");
        return 0; // unreachable
    }
    for (int i = 2; i <= top; ++i)
        if (Game::Lua::Type(L, i) == Game::Lua::TYPE_TABLE)
            Into(L, 1, i);
    Game::Lua::PushValue(L, 1); // return object
    return 1;
}

int __fastcall Script_CreateFromMixins(void *L) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::NewTable(L); // new object at index top + 1
    const int objIdx = top + 1;
    for (int i = 1; i <= top; ++i)
        if (Game::Lua::Type(L, i) == Game::Lua::TYPE_TABLE)
            Into(L, objIdx, i);
    Game::Lua::SetTop(L, objIdx); // the new table is the sole result
    return 1;
}

void RegisterFns() {
    Game::Lua::RegisterGlobalFunction("Mixin", &Script_Mixin);
    Game::Lua::RegisterGlobalFunction("CreateFromMixins", &Script_CreateFromMixins);
}

// Pure table helpers — register on both the in-game and glue states so they're
// always present (matching the "always available" intent).
const Game::ModuleAutoRegister _autoreg{&RegisterFns};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFns};

} // namespace

} // namespace BaseLib::Mixin
