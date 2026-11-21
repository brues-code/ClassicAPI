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

// `Mixin(object, ...)` and `CreateFromMixins(...)` as the 3.80.2 client has
// them: engine C functions (0x140C6A530 / 0x140C695B0, registered with the
// frame-script globals by 0x140C6B070), here in the DLL so they exist even
// with the addon disabled. `CreateAndInitFromMixin` (it calls a Lua `:Init`)
// stays Lua in the addon.
//
//   Mixin(object, ...)     every argument must be a table, and there must be
//                          at least one; each mixin's pairs are copied raw onto
//                          object (0x140A60290), a later one winning; returns
//                          object.
//   CreateFromMixins(...)  every argument must be a table; the new table's
//                          hash part is the largest mixin's node capacity
//                          (1 << lsizenode), at least 16 and at most 256; the
//                          copy as Mixin's; returns the new table.
//
// A bad call is the usage text through the pending-error formatter
// (0x1405AC380, so it ends with the taint line), raised verbatim
// (0x1405AC990 with no function name).

#include "baselib/Mixin.h"

#include "Game.h"
#include "table/Border.h"

namespace BaseLib::Mixin {

void Into(void *L, int dstIdx, int srcIdx) {
    Game::Lua::PushNil(L);
    const int key = Game::Lua::GetTop(L);
    while (Game::Lua::Next(L, srcIdx) != 0) {
        Game::Lua::PushValue(L, key);     // key copy
        Game::Lua::PushValue(L, key + 1); // value copy
        Game::Lua::RawSet(L, dstIdx);     // pops both copies
        Game::Lua::SetTop(L, key);        // pop the value, keep the key for Next()
    }
}

namespace {

constexpr const char *kMixinUsage = "Usage: local outObject = Mixin(object, ...)";
constexpr const char *kCreateUsage = "Usage: local object = CreateFromMixins(...)";
constexpr int kMinNodes = 16;
constexpr int kMaxNodes = 256;

bool AllTables(void *L, int top) {
    for (int i = 1; i <= top; ++i)
        if (Game::Lua::Type(L, i) != Game::Lua::TYPE_TABLE)
            return false;
    return true;
}

int __fastcall Script_Mixin(void *L) {
    const int top = Game::Lua::GetTop(L);
    if (top == 0 || !AllTables(L, top)) {
        Game::Lua::RaiseClientPendingError(L, nullptr, nullptr, kMixinUsage);
        return 0;
    }
    for (int i = 2; i <= top; ++i)
        Into(L, 1, i);
    Game::Lua::SetTop(L, 1);
    return 1;
}

int __fastcall Script_CreateFromMixins(void *L) {
    const int top = Game::Lua::GetTop(L);
    if (!AllTables(L, top)) {
        Game::Lua::RaiseClientPendingError(L, nullptr, nullptr, kCreateUsage);
        return 0;
    }
    int nodes = 0;
    for (int i = 1; i <= top; ++i) {
        const int capacity =
            1 << Game::Read<uint8_t>(Table::Border::TableAt(L, i), Offsets::OFF_LUA_TABLE_LSIZENODE);
        if (capacity > nodes)
            nodes = capacity;
    }
    nodes = nodes < kMinNodes ? kMinNodes : (nodes > kMaxNodes ? kMaxNodes : nodes);
    Game::Lua::CreateTable(L, 0, nodes);
    const int object = top + 1;
    for (int i = 1; i <= top; ++i)
        Into(L, object, i);
    return 1;
}

const Game::Doc::Field kMixinArgs[] = {
    Game::Doc::Req("object", "table", "The table that receives the fields."),
    Game::Doc::Vararg("table", "The mixins; a later one wins when two have the same key."),
};
const Game::Doc::Field kMixinRets[] = {
    Game::Doc::Req("object", "table", "The same table as the object argument."),
};
const Game::Doc::Function kMixin{"Copies every field of each mixin onto object.",
                                 kMixinArgs, kMixinRets, "FrameScriptGlobals"};

const Game::Doc::Field kCreateFromMixinsArgs[] = {
    Game::Doc::Vararg("table", "The mixins; a later one wins when two have the same key."),
};
const Game::Doc::Field kCreateFromMixinsRets[] = {
    Game::Doc::Req("object", "table"),
};
const Game::Doc::Function kCreateFromMixins{
    "Returns a new table with every field of each mixin copied onto it.",
    kCreateFromMixinsArgs, kCreateFromMixinsRets, "FrameScriptGlobals"};

void RegisterFns() {
    Game::Lua::RegisterGlobalFunction("Mixin", &Script_Mixin, &kMixin);
    Game::Lua::RegisterGlobalFunction("CreateFromMixins", &Script_CreateFromMixins,
                                      &kCreateFromMixins);
}

// Pure table helpers — register on both the in-game and glue states so they're
// always present (matching the "always available" intent).
const Game::ModuleAutoRegister _autoreg{&RegisterFns};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFns};

} // namespace

} // namespace BaseLib::Mixin
