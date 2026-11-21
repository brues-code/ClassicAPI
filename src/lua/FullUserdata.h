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
#include "lua/Boxed.h"

#include <cstddef>

// The 3.80.2 client's full-userdata objects: the C++ objects its generated
// API hands to Lua (the C_StringUtil formatters, the curve objects). Each is
// reference-counted; each Lua value is a userdata holding one reference. A
// class is the value's metatable, kept in the registry:
//   - __index: the class's methods (its FullUserdataMethodTable), then the
//     object's fields;
//   - __newindex: a method name or a metatable key is read-only ("Attempted
//     to assign to read-only key %s"); any other key goes to one field table
//     per object, shared by every value boxing it and dropped with the object;
//   - __eq: the same object; __tostring "<class name>: %p" of the object;
//     __gc drops the value's reference; __metatable false.
namespace Game::Lua::FullUserdata {

struct Object {
    virtual ~Object() = default;
    int refs = 0;
};

void Retain(Object *o);
// Null-safe. The last reference deletes the object and forgets its fields.
void Release(Object *o);

// Builds class `key`'s metatable in this Lua state unless it exists.
// `name` is what __tostring writes.
void CreateClass(void *L, const char *key, const char *name, const Boxed::Method *methods,
                 std::size_t count);

// Pushes a new value of class `key` for `o`, which gains a reference.
void Push(void *L, const char *key, Object *o);

// The object at `idx` when the value is of class `key`, else null.
Object *To(void *L, int idx, const char *key);

// The object at `idx` when the value is of any class built here, else null.
Object *ToAny(void *L, int idx);

} // namespace Game::Lua::FullUserdata
