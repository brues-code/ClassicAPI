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

#pragma once

#include "AutoList.h"

// The store behind every `t[k] = v`: luaV_settable (Offsets::FUN_LUAV_SETTABLE)
// serves OP_SETTABLE, OP_SETGLOBAL and lua_settable. MinHook allows one hook
// per address, so this module owns it and fans out, like Gc::Hooks. The store
// itself runs through Table::Freeze::SetTable; every subscriber hears about the
// write after it.
//
// Not covered: the raw writers (rawset, lua_rawset, lua_rawseti, table.insert)
// do not go through luaV_settable.
namespace Table::Settable {

// What a write was, read BEFORE the store: a __newindex the store calls can
// move the stack, so the TValue pointers luaV_settable got are not safe to
// read afterwards.
struct Write {
    const void *table; // the Table* written to, or null when `t` was not a table
    int keyType;       // Game::Lua::TYPE_* of the key
    const void *key;   // the key's GC object (a TString* for a string key)
};

// Runs after every store that did not raise, raw or through __newindex. It
// sits on the VM's hottest path: return after a pointer compare when the
// write is not yours. It may call Lua (lua_call), as a __newindex would.
using Callback = void (*)(void *L, const Write &write);
using AutoSubscribe = AutoList<struct AutoSubscribeTag, Callback>;

} // namespace Table::Settable
