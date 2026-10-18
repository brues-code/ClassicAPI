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

// Lua 5.1 string methods (`("asd"):upper()`, `s:format(...)`) on the 5.0 VM.
//
// Lua 5.0 has no string methods AT ALL — the shared string metatable with
// `__index = string` is a 5.1 addition (`createmetatable` in 5.1's
// lstrlib.c, which 4.3.4 ships unmodified). Nothing was stripped from this
// build: both places 5.1 changed match stock 5.0 source exactly —
//   * `lua_getmetatable` (0x006F3CF0) returns 0 for anything but a table or
//     userdata, where 5.1 reads the per-type slot `G(L)->mt[ttype(o)]`; and
//   * `luaT_gettmbyobj` (0x006F7BD0) hardcodes `&nilobject` for those same
//     types, where 5.1 does `luaH_getstr(G(L)->mt[ttype(o)], tmname[event])`.
// So this is the runtime half of the 5.1 backport (the syntax half is
// Transpile.cpp): one metatable `{ __index = string }` per Lua state, and a
// co-hook on each of those two functions that answers for strings exactly as
// 5.1's line does. Everything downstream is the engine's own unmodified code:
// the index continuation (0x006F7EE0) gets `__index` back from gettmbyobj
// and resolves it (table -> luaV_gettable, function -> callTM), so
// `("x").nonexistent` is nil, `("x").k = v` still errors (no __newindex),
// and a script that edits the metatable — `getmetatable("").__index.trim =
// f` (StatLogic) or a replacement `__index` — behaves as on 4.3.4.
//
// Cost per string method lookup is 4.3.4's: one luaH_getstr for "__index"
// in the metatable, then the lookup in the table it names. The only other
// work 5.1 also does is `tostring(s)` probing the metatable for `__tostring`
// (luaL_callmeta).
//
// Hotness: cold. gettmbyobj is reached only from fallbacks for values with
// no native operation (index/newindex/call on non-tables, arithmetic/order
// on non-numbers); table `__index` dispatch reads the table's OWN metatable
// via FUN_006F7D60's fasttm and never lands there. lua_getmetatable's
// callers are getmetatable, luaL_getmetafield (tostring) and
// luaL_checkudata. Both hooks hand every non-string to the original, so
// tables and userdata get the engine's exact result. No other Octo DLL
// hooks the Lua VM internals.
//
// What stays 5.0: only `__index` matters. A pair of strings has native
// concat, compare and coercing arithmetic, so the VM never asks for
// `__concat`/`__lt`/etc.; a mixed operand that does reach call_binTM now
// finds the metatable, which has no such field, and errors as before.
// `setmetatable` still rejects a string (it requires a table in 5.1 too), so
// the metatable can be edited but not replaced.
//
// State/GC lifecycle: one slot per Lua state (in-game + glue), keyed by the
// state's global_State (`*(L+0x10)`) so coroutine threads — which share G
// with their parent — resolve too. Each capture builds a fresh metatable and
// anchors it in that state's registry, so the hooks can never serve a
// GC-collected table; the metatable holds the `string` table, so even if an
// addon nils/replaces `_G.string`, methods keep the open-time table (5.1
// parity). Captures re-run per /reload (ModuleAutoRegister) and per glue
// boot (GlueModuleAutoRegister), overwriting the anchor so the previous
// metatable is released. Slots are cleared BEFORE their state can die, so a
// capture can never outlive the table it points at: PrepareForReload
// (called from FrameScript_Initialize_h, which fires on /reload AND /logout
// before the world Lua state is reset) clears the game slot, and the
// in-game registration clears the glue slot (once world scripts are
// loading, the glue state is gone; the next glue boot re-captures before
// any glue Lua runs). A cleared slot fails closed — strings have no
// metatable, exactly as stock 1.12, until the re-capture.
//
// Kill switch: `_classicapi_SetStringMethods(false)` (registered on both
// states) makes both hooks forward everything to the originals — exact
// stock behavior — for "is this backport breaking this addon?" diagnosis,
// mirroring `_classicapi_SetTranspileOption`.

#include "StringMethods.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace LuaSyntax::StringMethods {

namespace {

// `global_State *` slot in lua_State — the per-universe object shared by
// every coroutine thread of a state. Verified: luaT_gettmbyobj reads the
// tmname array at `*(L+0x10) + 0x80`, lua_setmetatable resolves the
// REGISTRY pseudo-index via `*(L+0x10) + 0x30`. Single-consumer offset,
// local per the Offsets.h rule.
constexpr uintptr_t OFF_L_GLOBALSTATE = 0x10;

// `L->top` slot in lua_State — lua_getmetatable writes the pushed TValue
// there and adds 0x10 (0x006F3CF0). Single-consumer, local.
constexpr uintptr_t OFF_L_TOP = 0x08;
// `G(L)->tmname[]` — the interned event-name strings luaT_gettmbyobj indexes
// by event (0x006F7BDB). Single-consumer, local.
constexpr uintptr_t OFF_G_TMNAME = 0x80;

// This build's TValue is 16 bytes with `tt` FIRST: {tt @ +0, ctx @ +4,
// value @ +8} — NOT stock 5.0's {value, tt} order. Verified from
// lua_getmetatable's push (tt=5, ctx=[VAR_FRAMESCRIPT_EXEC_CONTEXT],
// value=Table*) and the push helpers' 0x10 stride.
struct TValueMirror {
    int tt;
    int ctx;
    const void *value;
    int pad;
};
static_assert(sizeof(TValueMirror) == 16, "TValue is 16 bytes in this build");

// The string metatable for one Lua state — 5.1's `G(L)->mt[LUA_TSTRING]`.
// `G` doubles as the valid flag — nullptr means "no capture, pass
// everything through".
struct StateSlot {
    const void *G = nullptr;
    const void *mt = nullptr; // Table*
};
StateSlot g_gameSlot;
StateSlot g_glueSlot;

// Runtime kill switch — `_classicapi_SetStringMethods(false)` reverts to
// exact stock behavior (both hooks forward everything). Process-wide; both
// states share it.
bool g_enabled = true;

// Registry key anchoring the captured metatable against GC (rawset — the
// registry has no metatable). One key per state's registry; re-capture
// overwrites it, releasing the previous metatable.
constexpr const char *kAnchorKey = "__classicapi_string_mt";

// The string metatable for L's state, or nullptr when there is none.
const void *StringMetatable(void *L) {
    if (!g_enabled)
        return nullptr;
    const void *G = Game::Read<const void *>(L, OFF_L_GLOBALSTATE);
    if (g_gameSlot.G == G)
        return g_gameSlot.mt;
    if (g_glueSlot.G == G)
        return g_glueSlot.mt;
    return nullptr;
}

using GetTMByObj_t = const void *(__fastcall *)(void *L, const void *o, int event);
using LuaHGetStr_t = const void *(__fastcall *)(const void *t, const void *key);
GetTMByObj_t g_getTMByObjOriginal = nullptr;

// 5.1's `default: mt = G(L)->mt[ttype(o)]` for strings, then the same
// luaH_getstr(mt, tmname[event]) the original does for tables.
const void *__fastcall GetTMByObj_h(void *L, const void *o, int event) {
    if (*static_cast<const int *>(o) == Game::Lua::TYPE_STRING) {
        if (const void *mt = StringMetatable(L)) {
            const void *G = Game::Read<const void *>(L, OFF_L_GLOBALSTATE);
            const void *name = Game::Read<const void *>(G, OFF_G_TMNAME + event * 4);
            return reinterpret_cast<LuaHGetStr_t>(Offsets::FUN_LUAH_GETSTR)(mt, name);
        }
    }
    return g_getTMByObjOriginal(L, o, event);
}

const Game::HookAutoRegister _tmHook{Offsets::FUN_LUAT_GETTMBYOBJ,
                                     reinterpret_cast<void *>(&GetTMByObj_h),
                                     reinterpret_cast<void **>(&g_getTMByObjOriginal)};

using GetMetatable_t = int(__fastcall *)(void *L, int idx);
GetMetatable_t g_getMetatableOriginal = nullptr;

// 5.1's lua_getmetatable for strings: push the per-type metatable with the
// same TValue write the original uses for a table's (`sethvalue` +
// incr_top), so the caller's stack contract is unchanged.
int __fastcall GetMetatable_h(void *L, int idx) {
    if (g_getMetatableOriginal(L, idx))
        return 1;
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING)
        return 0;
    const void *mt = StringMetatable(L);
    if (mt == nullptr)
        return 0;
    auto &top = Game::Ref<TValueMirror *>(L, OFF_L_TOP);
    top->tt = Game::Lua::TYPE_TABLE;
    top->ctx = Game::Read<int>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    top->value = mt;
    ++top;
    return 1;
}

const Game::HookAutoRegister _mtHook{Offsets::LUA_GET_METATABLE,
                                     reinterpret_cast<void *>(&GetMetatable_h),
                                     reinterpret_cast<void **>(&g_getMetatableOriginal)};

// Builds `{ __index = _G.string }` for the CURRENT state (whatever
// VAR_LUA_STATE points at — the registration callbacks below run inside the
// right window for each) into `slot`, anchoring it in that state's registry.
// 5.1's createmetatable, run at the same point relative to scripts (before
// any addon or FrameXML Lua).
void CaptureFor(StateSlot &slot) {
    slot.G = nullptr;
    slot.mt = nullptr;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "string");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX); // [lib?]
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        Game::Lua::NewTable(L);                          // [lib, mt]
        Game::Lua::PushString(L, "__index");
        Game::Lua::PushValue(L, -3);
        Game::Lua::RawSet(L, -3);                        // mt.__index = lib
        Game::Lua::PushString(L, kAnchorKey);            // [lib, mt, key]
        Game::Lua::PushValue(L, -2);                     // [lib, mt, key, mt]
        Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX); // registry[key] = mt
        slot.mt = Game::Lua::ToPointer(L, -1);           // hvalue (Table*)
        slot.G = Game::Read<const void *>(L, OFF_L_GLOBALSTATE);
    }
    Game::Lua::SetTop(L, top);
}

// `_classicapi_SetStringMethods(enabled)` — diagnostic kill switch. While
// false, strings have no metatable and index errors as stock 1.12. Use only to answer "is this backport breaking this addon?".
int __fastcall Script_SetStringMethods(void *L) {
    g_enabled = Game::Lua::ToBoolean(L, 1) != 0;
    return 0;
}

void RegisterLuaFunctions() {
    // World scripts are loading ⇒ the glue state is gone (destroyed on the
    // glue→world handoff). Clear its slot so a future glue global_State
    // that lands at the same address can never match this stale capture.
    g_glueSlot.G = nullptr;
    g_glueSlot.mt = nullptr;
    CaptureFor(g_gameSlot);
    Game::Lua::RegisterGlobalFunction("_classicapi_SetStringMethods",
                                      &Script_SetStringMethods);
}
void RegisterGlueFunctions() {
    CaptureFor(g_glueSlot);
    Game::Lua::RegisterGlueFunction("_classicapi_SetStringMethods",
                                    &Script_SetStringMethods);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlueFunctions};

} // namespace

void PrepareForReload() {
    // The world Lua state is about to be reset (/reload) or destroyed
    // (/logout); its string metatable may not survive. Fail closed until the
    // next LoadScriptFunctions re-capture — a cleared slot means strings
    // have no metatable exactly as stock, and nothing can read a freed table.
    g_gameSlot.G = nullptr;
    g_gameSlot.mt = nullptr;
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace LuaSyntax::StringMethods
