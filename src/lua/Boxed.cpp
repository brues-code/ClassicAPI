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

#include "lua/Boxed.h"

namespace Game::Lua::Boxed {

namespace {

void SetFunction(void *L, const char *name, CFunction fn) {
    PushString(L, name);
    PushCClosure(L, fn, 0);
    SetTable(L, -3);
}

} // namespace

void CreateMetatable(void *L, const char *key, const Method *methods, size_t count, CFunction gc,
                     CFunction tostring, CFunction eq) {
    PushMetatable(L, key);
    const bool exists = Type(L, -1) == TYPE_TABLE;
    SetTop(L, -2);
    if (exists)
        return;
    PushString(L, key);
    NewTable(L);
    PushString(L, "__index");
    NewTable(L);
    for (size_t i = 0; i < count; ++i)
        SetFunction(L, methods[i].name, methods[i].fn);
    SetTable(L, -3);
    SetFunction(L, "__gc", gc);
    SetFunction(L, "__tostring", tostring);
    if (eq != nullptr)
        SetFunction(L, "__eq", eq);
    PushString(L, "__metatable");
    PushBool(L, false);
    SetTable(L, -3);
    RawSet(L, REGISTRY_INDEX);
}

void PushMetatable(void *L, const char *key) {
    PushString(L, key);
    RawGet(L, REGISTRY_INDEX);
}

void *To(void *L, int idx, const char *key) {
    if (Type(L, idx) != TYPE_USERDATA)
        return nullptr;
    if (idx < 0)
        idx = GetTop(L) + idx + 1;
    if (!GetMetatable(L, idx))
        return nullptr;
    PushMetatable(L, key);
    const bool ours = ToPointer(L, -1) == ToPointer(L, -2);
    SetTop(L, -3);
    return ours ? *static_cast<void **>(ToUserdata(L, idx)) : nullptr;
}

void Push(void *L, const char *key, void *p) {
    *static_cast<void **>(NewUserdata(L, sizeof(void *))) = p;
    PushMetatable(L, key);
    SetMetatable(L, -2);
}

} // namespace Game::Lua::Boxed
