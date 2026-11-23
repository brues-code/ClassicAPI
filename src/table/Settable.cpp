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

#include "table/Settable.h"

#include "Game.h"
#include "Offsets.h"
#include "table/Freeze.h"

namespace Table::Settable {

namespace {

Freeze::SetTable_t g_setTable = nullptr;

void __fastcall SetTable_h(void *L, const void *t, void *key, void *val) {
    const Write write{
        Game::Read<int>(t, Offsets::OFF_LUA_TVALUE_TT) == Game::Lua::TYPE_TABLE
            ? Game::Read<const void *>(t, Offsets::OFF_LUA_TVALUE_VALUE)
            : nullptr,
        Game::Read<int>(key, Offsets::OFF_LUA_TVALUE_TT),
        Game::Read<const void *>(key, Offsets::OFF_LUA_TVALUE_VALUE),
    };
    Freeze::SetTable(L, t, key, val, g_setTable);
    for (auto *s = AutoSubscribe::head; s != nullptr; s = s->next)
        s->fn(L, write);
}

const Game::HookAutoRegister _setTable{Offsets::FUN_LUAV_SETTABLE, reinterpret_cast<void *>(&SetTable_h),
                                       reinterpret_cast<void **>(&g_setTable)};

} // namespace

} // namespace Table::Settable
