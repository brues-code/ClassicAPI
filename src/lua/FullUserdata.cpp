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

// The metamethods are 3.80.2's shared ones (formatters 0x144592380, curves
// 0x144592000 and on): __index 0x140DAD5C0 / 0x140D29DE0, __newindex
// 0x140D56550 / 0x140D29F30, __eq 0x140D56710 / 0x140D2A0F0, __tostring
// "%s: %p" (0x140DAD990, the class's debug-name hook being a no-op). __dump,
// a debug dump, is not ported.

#include "lua/FullUserdata.h"

#include "lua/Arg.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Game::Lua::FullUserdata {

namespace {

// registry[kFieldsKey][object address] = the object's field table.
constexpr const char *kFieldsKey = "ClassicAPI.FullUserdata.fields";
// The metatable field holding the class's methods, 3.80.2's name for it.
constexpr const char *kMethodTableKey = "FullUserdataMethodTable";

// Every class built, so ToAny knows them all.
std::vector<const char *> g_classes;

// Pushes registry[kFieldsKey], creating it when `create`; false (nothing
// pushed) when it does not exist.
bool PushFieldsRoot(void *L, bool create) {
    PushString(L, kFieldsKey);
    RawGet(L, REGISTRY_INDEX);
    if (Type(L, -1) == TYPE_TABLE)
        return true;
    SetTop(L, -2);
    if (!create)
        return false;
    NewTable(L);
    PushString(L, kFieldsKey);
    PushValue(L, -2);
    RawSet(L, REGISTRY_INDEX);
    return true;
}

double FieldsKey(const Object *o) { return static_cast<double>(reinterpret_cast<uintptr_t>(o)); }

void ForgetFields(Object *o) {
    void *L = State();
    if (L == nullptr || !PushFieldsRoot(L, false))
        return;
    PushNumber(L, FieldsKey(o));
    PushNil(L);
    RawSet(L, -3);
    SetTop(L, -2);
}

int __fastcall Meta_gc(void *L) {
    auto **block = static_cast<Object **>(ToUserdata(L, 1));
    if (block != nullptr) {
        Release(*block);
        *block = nullptr;
    }
    return 0;
}

// The class name is the closure's upvalue.
int __fastcall Meta_tostring(void *L) {
    const char *name = ToString(L, UpvalueIndex(1));
    char text[96];
    std::snprintf(text, sizeof text, "%s: %p", name != nullptr ? name : "?", static_cast<void *>(ToAny(L, 1)));
    PushString(L, text);
    return 1;
}

int __fastcall Meta_eq(void *L) {
    const Object *a = ToAny(L, 1);
    PushBool(L, a != nullptr && a == ToAny(L, 2));
    return 1;
}

// Pushes metatable(stack 1)[kMethodTableKey][key at 2].
void PushMethod(void *L) {
    GetMetatable(L, 1);
    PushString(L, kMethodTableKey);
    RawGet(L, -2);
    PushValue(L, 2);
    GetTable(L, -2);
    Replace(L, -3);
    SetTop(L, -2);
}

// Methods first, then the object's fields.
int __fastcall Meta_index(void *L) {
    PushMethod(L);
    if (Type(L, -1) != TYPE_NIL)
        return 1;
    SetTop(L, -2);
    Object *o = ToAny(L, 1);
    if (o == nullptr || !PushFieldsRoot(L, false)) {
        PushNil(L);
        return 1;
    }
    PushNumber(L, FieldsKey(o));
    RawGet(L, -2);
    if (Type(L, -1) != TYPE_TABLE) {
        PushNil(L);
        return 1;
    }
    PushValue(L, 2);
    RawGet(L, -2);
    return 1;
}

// A metatable key or a method is read-only; any other key goes to the
// object's field table.
int __fastcall Meta_newindex(void *L) {
    GetMetatable(L, 1);
    PushValue(L, 2);
    RawGet(L, -2);
    bool readOnly = Type(L, -1) != TYPE_NIL;
    SetTop(L, 3);
    if (!readOnly) {
        PushMethod(L);
        readOnly = Type(L, -1) != TYPE_NIL;
        SetTop(L, 3);
    }
    if (readOnly) {
        PushValue(L, 2);
        const char *name = ToString(L, -1);
        char text[Arg::kErrorCap + 1];
        std::snprintf(text, sizeof text, "Attempted to assign to read-only key %s",
                      name != nullptr ? name : TypeName(L, Type(L, 2)));
        Arg::RaiseMessage(L, text);
        return 0;
    }
    Object *o = ToAny(L, 1);
    if (o == nullptr)
        return 0;
    PushFieldsRoot(L, true);
    const int root = GetTop(L);
    PushNumber(L, FieldsKey(o));
    RawGet(L, root);
    if (Type(L, -1) != TYPE_TABLE) {
        SetTop(L, -2);
        NewTable(L);
        PushNumber(L, FieldsKey(o));
        PushValue(L, -2);
        RawSet(L, root);
    }
    PushValue(L, 2);
    PushValue(L, 3);
    RawSet(L, -3);
    return 0;
}

void SetClosure(void *L, const char *key, CFunction fn) {
    PushString(L, key);
    PushCClosure(L, fn, 0);
    RawSet(L, -3);
}

} // namespace

void Retain(Object *o) { ++o->refs; }

void Release(Object *o) {
    if (o != nullptr && --o->refs == 0) {
        ForgetFields(o);
        delete o;
    }
}

// Builds the metatable, then moves its method table to kMethodTableKey and
// puts __index / __newindex in front of the fields and __tostring over the
// class name.
void CreateClass(void *L, const char *key, const char *name, const Boxed::Method *methods, std::size_t count) {
    if (std::find_if(g_classes.begin(), g_classes.end(), [&](const char *k) { return std::strcmp(k, key) == 0; }) ==
        g_classes.end())
        g_classes.push_back(key);
    Boxed::PushMetatable(L, key);
    const bool exists = Type(L, -1) == TYPE_TABLE;
    SetTop(L, -2);
    if (exists)
        return;
    Boxed::CreateMetatable(L, key, methods, count, &Meta_gc, &Meta_tostring, &Meta_eq);
    Boxed::PushMetatable(L, key);
    PushString(L, kMethodTableKey);
    PushString(L, "__index");
    RawGet(L, -3);
    RawSet(L, -3);
    SetClosure(L, "__index", &Meta_index);
    SetClosure(L, "__newindex", &Meta_newindex);
    PushString(L, "__tostring");
    PushString(L, name);
    PushCClosure(L, &Meta_tostring, 1);
    RawSet(L, -3);
    SetTop(L, -2);
}

void Push(void *L, const char *key, Object *o) {
    Retain(o);
    Boxed::Push(L, key, o);
}

Object *To(void *L, int idx, const char *key) { return static_cast<Object *>(Boxed::To(L, idx, key)); }

Object *ToAny(void *L, int idx) {
    for (const char *key : g_classes) {
        if (Object *o = To(L, idx, key))
            return o;
    }
    return nullptr;
}

} // namespace Game::Lua::FullUserdata
