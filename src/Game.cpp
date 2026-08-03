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

#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"

namespace Game {

namespace Lua {
// Each entry binds a typed function pointer in `Game::Lua::` to the
// corresponding raw VA in `Offsets`. The X-macro keeps the column-aligned
// list visually scannable and removes 27 lines of identical cast boilerplate.
#define CLASSICAPI_LUA_BINDINGS(F)              \
    F(IsNumber,    lua_isnumber,    LUA_IS_NUMBER)    \
    F(IsString,    lua_isstring,    LUA_IS_STRING)    \
    F(ToNumber,    lua_tonumber,    LUA_TO_NUMBER)    \
    F(ToBoolean,   lua_toboolean,   LUA_TO_BOOLEAN)   \
    F(ToString,    lua_tostring,    LUA_TO_STRING)    \
    F(StrLen,      lua_strlen,      LUA_STR_LEN)      \
    F(PushNumber,  lua_pushnumber,  LUA_PUSH_NUMBER)  \
    F(PushNil,     lua_pushnil,     LUA_PUSH_NIL)     \
    F(PushBoolean, lua_pushboolean, LUA_PUSH_BOOLEAN) \
    F(PushString,  lua_pushstring,  LUA_PUSH_STRING)  \
    F(PushLString, lua_pushlstring, LUA_PUSH_LSTRING) \
    F(PushValue,   lua_pushvalue,   LUA_PUSH_VALUE)   \
    F(PushCClosure,lua_pushcclosure,LUA_PUSH_CCLOSURE)\
    F(NewTable,    lua_newtable,    LUA_NEW_TABLE)    \
    F(GetTable,    lua_gettable,    LUA_GET_TABLE)    \
    F(RawGet,      lua_rawget,      LUA_RAW_GET)      \
    F(SetTable,    lua_settable,    LUA_SET_TABLE)    \
    F(RawSet,      lua_rawset,      LUA_RAW_SET)      \
    F(Insert,      lua_insert,      LUA_INSERT)       \
    F(Remove,      lua_remove,      LUA_REMOVE)       \
    F(GetTop,      lua_gettop,      LUA_GET_TOP)      \
    F(SetTop,      lua_settop,      LUA_SET_TOP)      \
    F(Call,        lua_call,        LUA_CALL)         \
    F(PCall,       lua_pcall,       LUA_PCALL)        \
    F(Next,        lua_next,        LUA_NEXT)         \
    F(Type,        lua_type,        LUA_TYPE)         \
    F(Error,       lua_error,       LUA_ERROR)        \
    F(ToPointer,   lua_topointer,   LUA_TO_POINTER)   \
    F(ToThread,    lua_tothread,    LUA_TO_THREAD)    \
    F(IsCFunction, lua_iscfunction, LUA_IS_C_FUNCTION)\
    F(XMove,       lua_xmove,       LUA_X_MOVE)       \
    F(NewThread,   lua_newthread,   LUA_NEW_THREAD)   \
    F(Resume,      lua_resume,      LUA_RESUME)       \
    F(Yield,       lua_yield,       LUA_YIELD)        \
    F(ArgError,    luaL_argerror,   LUAL_ARG_ERROR)   \
    F(SetN,        luaL_setn,       LUAL_SETN)         \
    F(CheckStack,  lua_checkstack,  LUA_CHECK_STACK)

#define CLASSICAPI_BIND_LUA(Name, Typedef, Offset) \
    const Typedef##_t Name = reinterpret_cast<Typedef##_t>(Offsets::Offset);
CLASSICAPI_LUA_BINDINGS(CLASSICAPI_BIND_LUA)
#undef CLASSICAPI_BIND_LUA
#undef CLASSICAPI_LUA_BINDINGS

namespace {
using FrameScript_RegisterFunction_t = void(__fastcall *)(const char *name, CFunction func);
using RegisterFrameMethods_t = void(__fastcall *)(const FrameMethodEntry *table, int count,
                                                  void *context);
} // namespace

void *State() {
    return *reinterpret_cast<void **>(static_cast<uintptr_t>(Offsets::VAR_LUA_STATE));
}

namespace {
using FrameScriptPushObject_t = void(__fastcall *)(void *L, int idx, int unused);
using FrameScriptGetObject_t = void *(__fastcall *)(void *L, int idx);
} // namespace

void *ResolveObject(void *L, int idx) {
    auto PushObject = reinterpret_cast<FrameScriptPushObject_t>(
        Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);
    auto GetObject = reinterpret_cast<FrameScriptGetObject_t>(
        Offsets::FUN_FRAMESCRIPT_GET_OBJECT);
    PushObject(L, idx, 0);
    void *result = GetObject(L, -1);
    SetTop(L, -2);
    return result;
}

void RegisterGlobalFunction(const char *name, CFunction func) {
    auto fn = reinterpret_cast<FrameScript_RegisterFunction_t>(
        Offsets::FUN_FRAMESCRIPT_REGISTER_FUNCTION);
    fn(name, func);
}

// Aliased to the same engine entry; the engine reads VAR_LUA_STATE
// internally to decide which state to write to, and the glue hook
// runs while that pointer is set to the glue state. Wrapped as a
// named function (rather than `using RegisterGlueFunction = ...`) so
// callers express intent at the call site.
void RegisterGlueFunction(const char *name, CFunction func) {
    auto fn = reinterpret_cast<FrameScript_RegisterFunction_t>(
        Offsets::FUN_FRAMESCRIPT_REGISTER_FUNCTION);
    fn(name, func);
}

void RegisterFrameMethods(void *context, const FrameMethodEntry *table, int count) {
    auto fn = reinterpret_cast<RegisterFrameMethods_t>(Offsets::FUN_REGISTER_FRAME_METHODS);
    fn(table, count, context);
}

// Looks up `_G[name]`. If absent, creates a fresh table and binds it.
// Leaves the resulting table on top of the stack.
namespace {
void EnsureGlobalTable(void *L, const char *name) {
    PushString(L, name);
    GetTable(L, GLOBALS_INDEX);
    if (Type(L, -1) == TYPE_TABLE)
        return;
    SetTop(L, -2);                 // pop the non-table.        []
    NewTable(L);                   //                           [tbl]
    PushValue(L, -1);              //                           [tbl, tbl]
    PushString(L, name);           //                           [tbl, tbl, name]
    Insert(L, -2);                 //                           [tbl, name, tbl]
    SetTable(L, GLOBALS_INDEX);    // _G[name] = tbl; pops k+v. [tbl]
}
} // namespace

// Registers `func` at `_G[tableName][methodName]`. If the namespace
// doesn't already exist, creates an empty table for it.
void RegisterTableFunction(const char *tableName, const char *methodName, CFunction func) {
    void *L = State();
    if (L == nullptr)
        return;
    EnsureGlobalTable(L, tableName);     // [tbl]
    PushString(L, methodName);           // [tbl, methodName]
    PushCClosure(L, func, 0);            // [tbl, methodName, closure]
    SetTable(L, -3);                     // tbl[m]=c; pops k+v. [tbl]
    SetTop(L, -2);                       // pop tbl. []
}

void RegisterIntegerEnum(const char *parent, const char *sub,
                         const EnumIntegerEntry *entries, int count) {
    void *L = State();
    if (L == nullptr)
        return;
    EnsureGlobalTable(L, parent); // [parentTbl]
    PushString(L, sub);           // [parentTbl, subName]
    NewTable(L);                  // [parentTbl, subName, subTbl]
    for (int i = 0; i < count; ++i) {
        PushString(L, entries[i].key);
        PushNumber(L, static_cast<double>(entries[i].value));
        SetTable(L, -3); // subTbl[key] = value
    }
    SetTable(L, -3); // parentTbl[sub] = subTbl
    SetTop(L, -2);   // pop parentTbl
}

void SetFieldNumber(void *L, const char *key, double value) {
    PushString(L, key);
    PushNumber(L, value);
    SetTable(L, -3);
}

void SetFieldString(void *L, const char *key, const char *value) {
    PushString(L, key);
    PushString(L, value != nullptr ? value : "");
    SetTable(L, -3);
}

void SetFieldBool(void *L, const char *key, bool value) {
    PushString(L, key);
    PushBoolean(L, static_cast<int>(value));
    SetTable(L, -3);
}

void SetGlobalNumber(void *L, const char *name, double value) {
    PushString(L, name);
    PushNumber(L, value);
    RawSet(L, GLOBALS_INDEX);
}

bool PushGlobalFunction(void *L, const char *name) {
    PushString(L, name);
    GetTable(L, GLOBALS_INDEX);
    if (Type(L, -1) == TYPE_FUNCTION)
        return true;
    SetTop(L, -2); // pop the non-function
    return false;
}

void CallGlobal(void *L, const char *name) {
    const int top = GetTop(L);
    if (PushGlobalFunction(L, name))
        Call(L, 0, 0);
    SetTop(L, top);
}

bool CallGlobalString(void *L, const char *name, const char *arg) {
    const int top = GetTop(L);
    const bool called = PushGlobalFunction(L, name);
    if (called) {
        PushString(L, arg);
        Call(L, 1, 0);
    }
    SetTop(L, top);
    return called;
}

void PushLocalizedString(void *L, const char *globalName, const char *fallback) {
    PushString(L, globalName);
    GetTable(L, GLOBALS_INDEX);
    if (Type(L, -1) != TYPE_STRING) {
        // Replace the non-string at top with the fallback so callers
        // always see exactly one string at -1.
        SetTop(L, GetTop(L) - 1);
        PushString(L, fallback);
    }
}

// In-place `string.gsub(stack[-1], pattern, replacement)` — replaces
// the string at the top of the stack with the substitution result.
// Discards gsub's second return (the substitution count) by
// requesting only one return from `Call`.
static void GsubInPlace(void *L, const char *pattern, const char *replacement) {
    PushString(L, "string");
    GetTable(L, GLOBALS_INDEX);
    PushString(L, "gsub");
    GetTable(L, -2);             // [..., str, string, gsub]
    Remove(L, -2);                // [..., str, gsub]
    Insert(L, -2);                // [..., gsub, str]
    PushString(L, pattern);
    PushString(L, replacement);
    Call(L, 3, 1);                // [..., result]
}

void PushLocalizedFormatInt(void *L, const char *globalName,
                            const char *fallback, int n) {
    // Resolve `string.format` from _G.
    PushString(L, "format");
    GetTable(L, GLOBALS_INDEX);
    PushLocalizedString(L, globalName, fallback);

    // Expand WoW's `|4singular:plural;` (and 3-form
    // `|4nom:gen_sing:plural;`) pluralization template by piggybacking
    // on Lua's `string.gsub`. The 2- vs 3-form distinction is locale
    // grammar (Slavic counting has a separate genitive-singular form
    // for 2..4); we collapse to nominative/plural since vanilla has
    // no proper locale-aware counting rules. 3-form pattern runs
    // first because it's more specific. Vanilla's `string.format`
    // doesn't process `|4` natively, so without this the template
    // would render literally.
    const char *picker3 = (n == 1) ? "%1" : "%3";
    const char *picker2 = (n == 1) ? "%1" : "%2";
    GsubInPlace(L, "|4([^:;]+):([^:;]+):([^;]+);", picker3);
    GsubInPlace(L, "|4([^:;]+):([^;]+);", picker2);

    PushNumber(L, static_cast<double>(n));
    Call(L, 2, 1);                // format(expandedFmt, n) → string
}
} // namespace Lua

namespace Console {
namespace {
using RegisterCommand_t = int(__fastcall *)(const char *name, void *handler,
                                            int category, const char *description);
using Write_t = void(__fastcall *)(const char *line, int colorFlag);
} // namespace

void RegisterCommand(const char *name, CommandHandler handler, int category,
                     const char *description) {
    auto fn = reinterpret_cast<RegisterCommand_t>(Offsets::FUN_CONSOLE_COMMAND_REGISTER);
    fn(name, reinterpret_cast<void *>(handler), category, description);
}

void Write(const char *line) {
    auto fn = reinterpret_cast<Write_t>(Offsets::FUN_CONSOLE_WRITE);
    fn(line, 0);
}
} // namespace Console

namespace {
ModuleAutoRegister *g_moduleHead = nullptr;
GlueModuleAutoRegister *g_glueModuleHead = nullptr;
HookAutoRegister *g_hookHead = nullptr;
} // namespace

ModuleAutoRegister::ModuleAutoRegister(Fn f) : fn(f), next(g_moduleHead) {
    g_moduleHead = this;
}

void RunModuleRegistrations() {
    for (auto *node = g_moduleHead; node != nullptr; node = node->next)
        node->fn();
}

GlueModuleAutoRegister::GlueModuleAutoRegister(Fn f)
    : fn(f), next(g_glueModuleHead) {
    g_glueModuleHead = this;
}

void RunGlueModuleRegistrations() {
    for (auto *node = g_glueModuleHead; node != nullptr; node = node->next)
        node->fn();
}

HookAutoRegister::HookAutoRegister(uintptr_t target, void *hook, void **original)
    : target(target), hook(hook), original(original), next(g_hookHead) {
    g_hookHead = this;
}

bool RunHookRegistrations() {
    for (auto *node = g_hookHead; node != nullptr; node = node->next) {
        auto *targetPtr = reinterpret_cast<LPVOID>(node->target);
        if (MH_CreateHook(targetPtr, node->hook,
                          reinterpret_cast<LPVOID *>(node->original)) != MH_OK)
            return false;
        if (MH_EnableHook(targetPtr) != MH_OK)
            return false;
    }
    return true;
}

} // namespace Game
