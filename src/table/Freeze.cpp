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
#include "gc/Hooks.h"
#include "gc/Tag.h"
#include "table/Border.h"

#include <ankerl/unordered_dense.h>

namespace Table::Freeze {

// `table.freeze(t)` / `table.isfrozen(t)` as the 3.80.2 client has them
// (0x140D32E10 / 0x140D32ED0, registered with table.count / create and
// C_TableUtil.FindIndexedMismatch by 0x140D32F40 in both states). The full
// map is docs/notes/table-freeze-3802.md.
//
// 3.80.2 keeps a frozen byte in the Table (+0x45, set by 0x142A0F090, never
// cleared) and checks it inside the VM's setters:
//   luaH_set / luaH_setnum / luaH_setstr  "attempted to perform indexed
//                                          assignment on a frozen table"
//   luaV_settable (0x142A0DF00)           a frozen table never takes the raw
//                                          store: the write goes to __newindex
//                                          (function → call it, table → retry on
//                                          it); only with none does it raise.
//   lua_setmetatable (0x142A052A0)        "attempted to replace metatable of a
//                                          frozen table"
// so rawset / rawseti and every table library writer (insert, remove, sort,
// wipe via lua_rawset) raise on the first write and change nothing.
//
// 1.12's Table header has no spare byte (+6/+7 are flags/lsizenode), so the
// flag is this set of Table*, dropped on create and free through Gc::Hooks (a
// create also drops it, so an address reused after an inline free — wow_turbo's
// sweep — cannot inherit it) and cleared when a Lua state is torn down.
//
// The checks sit on the setters' CALLERS, not on luaH_set / luaH_setnum:
// 1.12's luaV_settable calls luaH_set BEFORE it looks at __newindex (stock
// 5.0), so a check inside luaH_set would raise where 3.80.2 reroutes. luaH_set's
// other callers are lua_rawset, adjust_varargs, the rehash and the compiler;
// luaH_setnum's are lua_rawseti, adjust_varargs, the rehash and OP_SETLIST
// (0x6F95B1). All but the two API functions touch only a table they just made.
// 5.0 has no luaH_setstr. The table library's 5.0 length (luaL_setn) is written
// BEFORE the element in tinsert / tremove; it is left alone for a frozen table,
// so the element write that follows raises with nothing changed.
//
// Every hook passes straight through while nothing is frozen.

namespace {

constexpr const char *kAssign = "perform indexed assignment on";
constexpr int kTmNewIndex = 1; // TM_NEWINDEX, luaV_settable's `push 1` (0x6F7FD3)
constexpr int kMaxTagLoop = 100;

ankerl::unordered_dense::set<const void *> g_frozen;

bool IsFrozen(const void *t) { return !g_frozen.empty() && g_frozen.contains(t); }

// The table at a stack index is frozen. The empty test comes first so the
// index walk only runs while something is frozen.
bool FrozenAt(void *L, int idx) {
    return !g_frozen.empty() && g_frozen.contains(Table::Border::TableAt(L, idx));
}

bool IsFrozenValue(const void *o) {
    return Game::Read<int>(o, Offsets::OFF_LUA_TVALUE_TT) == Game::Lua::TYPE_TABLE &&
           IsFrozen(Game::Read<const void *>(o, Offsets::OFF_LUA_TVALUE_VALUE));
}

using RunError_t = void(__cdecl *)(void *L, const char *fmt, ...);
const auto RunError = reinterpret_cast<RunError_t>(Offsets::FUN_LUAG_RUNERROR);

[[noreturn]] void RaiseFrozen(void *L, const char *verb) {
    RunError(L, "attempted to %s a frozen table", verb);
    __assume(0);
}

// --- luaV_settable ---------------------------------------------------------

using SetTable_t = void(__fastcall *)(void *L, const void *t, void *key, void *val);
using GetTmByObj_t = const void *(__fastcall *)(void *L, const void *o, int event);
using CallTM_t = void(__fastcall *)(void *L, const void *f, const void *p1, const void *p2, const void *p3);
SetTable_t g_setTable = nullptr;

// 3.80.2's frozen branch of luaV_settable: a frozen table goes to its
// __newindex. A value that is not a frozen table continues in the engine's
// own luaV_settable (its loop count starts again there).
void __fastcall SetTable_h(void *L, const void *t, void *key, void *val) {
    if (g_frozen.empty()) {
        g_setTable(L, t, key, val);
        return;
    }
    for (int loop = 0; loop < kMaxTagLoop; ++loop) {
        if (!IsFrozenValue(t)) {
            g_setTable(L, t, key, val);
            return;
        }
        const void *tm =
            reinterpret_cast<GetTmByObj_t>(Offsets::FUN_LUAT_GETTMBYOBJ)(L, t, kTmNewIndex);
        const int tt = Game::Read<int>(tm, Offsets::OFF_LUA_TVALUE_TT);
        if (tt == Game::Lua::TYPE_NIL)
            RaiseFrozen(L, kAssign);
        if (tt == Game::Lua::TYPE_FUNCTION) {
            reinterpret_cast<CallTM_t>(Offsets::FUN_LUA_CALLTM)(L, tm, t, key, val);
            return;
        }
        t = tm;
    }
    RunError(L, "loop in settable");
}

// --- lua_rawset / lua_rawseti / lua_setmetatable / luaL_setn ----------------

using RawSet_t = void(__fastcall *)(void *L, int idx);
using RawSetI_t = void(__fastcall *)(void *L, int idx, int n);
using SetMetatable_t = int(__fastcall *)(void *L, int objindex);
using SetN_t = void(__fastcall *)(void *L, int t, int n);
RawSet_t g_rawSet = nullptr;
RawSetI_t g_rawSetI = nullptr;
SetMetatable_t g_setMetatable = nullptr;
SetN_t g_setN = nullptr;

void __fastcall RawSet_h(void *L, int idx) {
    if (FrozenAt(L, idx))
        RaiseFrozen(L, kAssign);
    g_rawSet(L, idx);
}

void __fastcall RawSetI_h(void *L, int idx, int n) {
    if (FrozenAt(L, idx))
        RaiseFrozen(L, kAssign);
    g_rawSetI(L, idx, n);
}

int __fastcall SetMetatable_h(void *L, int objindex) {
    if (FrozenAt(L, objindex))
        RaiseFrozen(L, "replace metatable of");
    return g_setMetatable(L, objindex);
}

void __fastcall SetN_h(void *L, int t, int n) {
    if (FrozenAt(L, t))
        return;
    g_setN(L, t, n);
}

// --- lifetime ----------------------------------------------------------------

void Forget(const void *object) {
    if (!g_frozen.empty())
        g_frozen.erase(object);
}

void Clear() { g_frozen.clear(); }

// --- Lua -----------------------------------------------------------------------

// 0x140D32E10: a table only; when not yet frozen, code running tainted may only
// freeze a table that carries its own taint, then the flag is set. Returns the
// value on top of the stack (it pushes nothing), so freeze(t) returns t.
// 1.12's taint is the context word, and a table's own taint is its Gc::Tag
// (3.3.5's creation tag).
int __fastcall Script_table_freeze(void *L) {
    Game::Lua::CheckType(L, 1, Game::Lua::TYPE_TABLE);
    const void *t = Table::Border::TableAt(L, 1);
    if (!g_frozen.contains(t)) {
        const char *current = Game::Read<const char *>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
        const char *owner = Gc::Tag::Of(t);
        if (current != nullptr && current != owner) {
            if (owner == nullptr)
                Game::Lua::Error(L,
                                 "attempted to freeze a table not owned by the calling function "
                                 "(expected '%s')",
                                 current);
            else
                Game::Lua::Error(L,
                                 "attempted to freeze a table not owned by the calling function "
                                 "(expected '%s', got '%s')",
                                 current, owner);
        }
        g_frozen.insert(t);
    }
    return 1;
}

// 0x140D32ED0.
int __fastcall Script_table_isfrozen(void *L) {
    Game::Lua::CheckType(L, 1, Game::Lua::TYPE_TABLE);
    Game::Lua::PushBoolean(L, FrozenAt(L, 1));
    return 1;
}

const Game::Doc::Field kTableArg[] = {
    Game::Doc::Req("tbl", "table"),
};
const Game::Doc::Field kFreezeRets[] = {
    Game::Doc::Req("tbl", "table"),
};
const Game::Doc::Field kIsFrozenRets[] = {
    Game::Doc::Req("isFrozen", "boolean"),
};
const Game::Doc::Function kFreeze{"Makes a table read-only and returns it.", kTableArg, kFreezeRets};
const Game::Doc::Function kIsFrozen{"Returns true if the table is read-only.", kTableArg, kIsFrozenRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("table", "freeze", &Script_table_freeze, &kFreeze);
    Game::Lua::RegisterTableFunction("table", "isfrozen", &Script_table_isfrozen, &kIsFrozen);
}

const Game::HookAutoRegister _setTable{Offsets::FUN_LUAV_SETTABLE, reinterpret_cast<void *>(&SetTable_h),
                                       reinterpret_cast<void **>(&g_setTable)};
const Game::HookAutoRegister _rawSet{Offsets::LUA_RAW_SET, reinterpret_cast<void *>(&RawSet_h),
                                     reinterpret_cast<void **>(&g_rawSet)};
const Game::HookAutoRegister _rawSetI{Offsets::LUA_RAW_SET_I, reinterpret_cast<void *>(&RawSetI_h),
                                      reinterpret_cast<void **>(&g_rawSetI)};
const Game::HookAutoRegister _setMetatable{Offsets::LUA_SET_METATABLE,
                                           reinterpret_cast<void *>(&SetMetatable_h),
                                           reinterpret_cast<void **>(&g_setMetatable)};
const Game::HookAutoRegister _setN{Offsets::LUAL_SETN, reinterpret_cast<void *>(&SetN_h),
                                   reinterpret_cast<void **>(&g_setN)};

const Gc::Hooks::AutoSubscribe _gc{&Forget, &Forget};
const Game::ReloadAutoRegister _reload{&Clear};
const Game::GlueBootAutoRegister _glueBoot{&Clear};
const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Table::Freeze
