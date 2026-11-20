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

#include "Game.h"

#include <cstddef>

// A userdata class: each value is a full userdata holding one C++ pointer, and
// the class's metatable is kept in the registry under a string key. The
// metatable's identity is what marks a value as the class's, as luaL_checkudata
// does in later Lua versions.
namespace Game::Lua::Boxed {

struct Method {
    const char *name;
    CFunction fn;
};

// Builds the class's metatable in this Lua state unless it exists: `__index`
// with the methods, `__gc`, `__tostring`, `__eq` when one is given, and
// `__metatable` false so scripts cannot reach it.
void CreateMetatable(void *L, const char *key, const Method *methods, size_t count, CFunction gc,
                     CFunction tostring, CFunction eq);
template <size_t N>
void CreateMetatable(void *L, const char *key, const Method (&methods)[N], CFunction gc, CFunction tostring,
                     CFunction eq = nullptr) {
    CreateMetatable(L, key, methods, N, gc, tostring, eq);
}

// Pushes the metatable under `key` (nil before CreateMetatable).
void PushMetatable(void *L, const char *key);

// The pointer boxed at stack `idx` when the value is a userdata with the
// metatable under `key`, else nullptr.
void *To(void *L, int idx, const char *key);

// Pushes a new userdata boxing `p`, with the metatable under `key`.
void Push(void *L, const char *key, void *p);

} // namespace Game::Lua::Boxed
