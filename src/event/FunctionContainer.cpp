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

// `C_FunctionContainers.CreateCallback(func)` and the container userdata, as
// the 3.80.2 client has them (FUN_1418b6050, methods FUN_140189f30,
// metatable at 0x144592b60):
//   - CreateCallback takes a Lua function only; anything else, a C function
//     included, raises "Usage: C_FunctionContainers.CreateCallback(func)".
//   - `Cancel()` releases the function and marks the container cancelled;
//     calling it again does nothing. `IsCancelled()`. `Invoke(...)` calls the
//     function with the arguments after self, protected, its errors going to
//     the error handler, and returns nothing; on a cancelled container it does
//     nothing. Each raises "Usage: callback:<Method>(...)" for a bad self.
//   - `__index` reads the methods, then a table of fields kept per object;
//     `__newindex` writes that table and refuses a method name with
//     "Attempted to assign to read-only key %s". `__eq` compares the
//     containers, `__tostring` gives "LuaFunctionContainer: <address>", and
//     there is no `__call`.

#include "event/FunctionContainer.h"

#include "Game.h"
#include "lua/Boxed.h"

#include <cstdint>
#include <cstdio>

namespace Event::FunctionContainer {

struct Container {
    int fnRef = -1;
    uint32_t generation = 0; // 0 once cancelled
    int fieldsRef = -1;
    uint32_t fieldsGeneration = 0;
    int refs = 0;
};

namespace {

constexpr const char *kKey = "LuaFunctionContainer";
constexpr const char *kMethodsKey = "LuaFunctionContainer.methods";

// The Lua state a container belongs to; a /reload makes a new one.
uint32_t g_generation = 1;

bool Live(const Container *c) { return c->fnRef != -1 && c->generation == g_generation; }

void FreeFunction(Container *c) {
    void *L = Game::Lua::State();
    if (L != nullptr && Live(c))
        Game::Lua::Unref(L, Game::Lua::REGISTRY_INDEX, c->fnRef);
    c->fnRef = -1;
    c->generation = 0;
}

void FreeFields(Container *c) {
    void *L = Game::Lua::State();
    if (L != nullptr && c->fieldsRef != -1 && c->fieldsGeneration == g_generation)
        Game::Lua::Unref(L, Game::Lua::REGISTRY_INDEX, c->fieldsRef);
    c->fieldsRef = -1;
}

Container *Create(void *L, int idx) {
    Game::Lua::PushValue(L, idx);
    auto *c = new Container;
    c->fnRef = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
    c->generation = g_generation;
    c->refs = 1;
    return c;
}

bool IsLuaFunction(void *L, int idx) {
    return Game::Lua::Type(L, idx) == Game::Lua::TYPE_FUNCTION && !Game::Lua::IsCFunction(L, idx);
}

Container *Self(void *L, int idx) {
    return static_cast<Container *>(Game::Lua::Boxed::To(L, idx, kKey));
}

bool IsMethodName(void *L, int keyIdx) {
    Game::Lua::PushString(L, kMethodsKey);
    Game::Lua::RawGet(L, Game::Lua::REGISTRY_INDEX);
    Game::Lua::PushValue(L, keyIdx);
    Game::Lua::RawGet(L, -2);
    const bool method = Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL;
    Game::Lua::SetTop(L, -3);
    return method;
}

int __fastcall Script_CreateCallback(void *L) {
    if (!IsLuaFunction(L, 1)) {
        Game::Lua::Error(L, "Usage: C_FunctionContainers.CreateCallback(func)");
        return 0;
    }
    Game::Lua::Boxed::Push(L, kKey, Create(L, 1));
    return 1;
}

int __fastcall Script_Cancel(void *L) {
    Container *c = Self(L, 1);
    if (c == nullptr) {
        Game::Lua::Error(L, "Usage: callback:Cancel()");
        return 0;
    }
    FreeFunction(c);
    return 0;
}

int __fastcall Script_IsCancelled(void *L) {
    const Container *c = Self(L, 1);
    if (c == nullptr) {
        Game::Lua::Error(L, "Usage: callback:IsCancelled()");
        return 0;
    }
    Game::Lua::PushBool(L, !Live(c));
    return 1;
}

int __fastcall Script_Invoke(void *L) {
    const Container *c = Self(L, 1);
    if (c == nullptr) {
        Game::Lua::Error(L, "Usage: callback:Invoke([args])");
        return 0;
    }
    if (!Live(c))
        return 0;
    const int top = Game::Lua::GetTop(L);
    const int errfunc = Game::Lua::PushErrorHandler(L);
    if (!PushFunction(L, c)) {
        Game::Lua::SetTop(L, top);
        Game::Lua::Error(L, "Usage: callback([params])");
        return 0;
    }
    for (int i = 2; i <= top; ++i)
        Game::Lua::PushValue(L, i);
    Game::Lua::PCall(L, top - 1, 0, errfunc);
    Game::Lua::SetTop(L, top);
    return 0;
}

// `__index(self, key)`: a method, else the object's own field.
int __fastcall Meta_Index(void *L) {
    Game::Lua::PushString(L, kMethodsKey);
    Game::Lua::RawGet(L, Game::Lua::REGISTRY_INDEX);
    Game::Lua::PushValue(L, 2);
    Game::Lua::RawGet(L, -2);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL)
        return 1;
    const Container *c = Self(L, 1);
    if (c == nullptr || c->fieldsRef == -1 || c->fieldsGeneration != g_generation) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, c->fieldsRef);
    Game::Lua::PushValue(L, 2);
    Game::Lua::RawGet(L, -2);
    return 1;
}

// `__newindex(self, key, value)`.
int __fastcall Meta_NewIndex(void *L) {
    Container *c = Self(L, 1);
    if (c == nullptr)
        return 0;
    if (IsMethodName(L, 2)) {
        const char *key = Game::Lua::ToString(L, 2);
        Game::Lua::Error(L, "Attempted to assign to read-only key %s", key != nullptr ? key : "?");
        return 0;
    }
    if (c->fieldsRef == -1 || c->fieldsGeneration != g_generation) {
        Game::Lua::NewTable(L);
        c->fieldsRef = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
        c->fieldsGeneration = g_generation;
    }
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, c->fieldsRef);
    Game::Lua::PushValue(L, 2);
    Game::Lua::PushValue(L, 3);
    Game::Lua::RawSet(L, -3);
    return 0;
}

int __fastcall Meta_Gc(void *L) {
    if (Container *c = Self(L, 1)) {
        FreeFields(c);
        Release(c);
    }
    return 0;
}

int __fastcall Meta_ToString(void *L) {
    char text[64];
    std::snprintf(text, sizeof(text), "%s: %p", kKey, Game::Lua::ToPointer(L, 1));
    Game::Lua::PushString(L, text);
    return 1;
}

int __fastcall Meta_Eq(void *L) {
    const Container *a = Self(L, 1);
    Game::Lua::PushBool(L, a != nullptr && a == Self(L, 2));
    return 1;
}

void SetFunction(void *L, const char *name, Game::Lua::CFunction fn) {
    Game::Lua::PushString(L, name);
    Game::Lua::PushCClosure(L, fn, 0);
    Game::Lua::RawSet(L, -3);
}

// Builds the methods table and the metatable in this Lua state, under the keys
// Game::Lua::Boxed reads.
void CreateMetatable(void *L) {
    Game::Lua::PushString(L, kMethodsKey);
    Game::Lua::NewTable(L);
    SetFunction(L, "Cancel", &Script_Cancel);
    SetFunction(L, "IsCancelled", &Script_IsCancelled);
    SetFunction(L, "Invoke", &Script_Invoke);
    Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX);

    Game::Lua::PushString(L, kKey);
    Game::Lua::NewTable(L);
    SetFunction(L, "__index", &Meta_Index);
    SetFunction(L, "__newindex", &Meta_NewIndex);
    SetFunction(L, "__gc", &Meta_Gc);
    SetFunction(L, "__tostring", &Meta_ToString);
    SetFunction(L, "__eq", &Meta_Eq);
    Game::Lua::PushString(L, "__metatable");
    Game::Lua::PushBool(L, false);
    Game::Lua::RawSet(L, -3);
    Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX);
}

const Game::Doc::Field kCreateArgs[] = {
    Game::Doc::Req("func", "function"),
};
const Game::Doc::Field kCreateRets[] = {
    Game::Doc::Req("callback", "LuaFunctionContainer",
                   "Has the methods Cancel(), IsCancelled() and Invoke(...)."),
};
const Game::Doc::Function kCreateCallback{
    "Returns a callback object for a function, to pass to RegisterEventCallback and to "
    "cancel later.",
    kCreateArgs, kCreateRets};

void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    CreateMetatable(L);
    Game::Lua::SetTop(L, top);
    Game::Lua::RegisterTableFunction("C_FunctionContainers", "CreateCallback",
                                     &Script_CreateCallback, &kCreateCallback);
}

// The containers of the old state are skipped from here on; the registry
// refs they hold die with that state.
void PrepareForReload() { ++g_generation; }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

Container *FromCallbackArg(void *L, int idx) {
    if (IsLuaFunction(L, idx))
        return Create(L, idx);
    Container *c = Self(L, idx);
    if (c != nullptr)
        Retain(c);
    return c;
}

void Retain(Container *c) { ++c->refs; }

void Release(Container *c) {
    if (--c->refs > 0)
        return;
    FreeFunction(c);
    FreeFields(c);
    delete c;
}

bool PushFunction(void *L, const Container *c) {
    if (!Live(c))
        return false;
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, c->fnRef);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, -2);
        return false;
    }
    return true;
}

} // namespace Event::FunctionContainer
