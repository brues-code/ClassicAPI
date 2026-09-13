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

#include <cstddef>
#include <cstdint>

namespace Game {

// --- Typed engine-memory access ---------------------------------------------
//
// The canonical way to read/write an engine object field or a fixed-VA global,
// replacing the `*reinterpret_cast<const T *>(base + Offsets::OFF_X)` idiom
// (and its double-cast variant when `base` isn't a byte pointer). Offsets stay
// in Offsets.h per the single-source-of-truth rule — these only centralize the
// cast noise, they add no checking.
//
//   Game::Read<int>(obj, Offsets::OFF_FIELD)      field read
//   Game::Ref<uint32_t>(obj, Offsets::OFF_FIELD)  writable field lvalue
//   Game::Ptr<float>(obj, Offsets::OFF_FIELD)     pointer TO a field (arrays,
//                                                 out-params for engine calls)
//   Game::Read<float>(Offsets::VAR_GLOBAL)        fixed-VA global read
//   Game::Ref<int>(Offsets::VAR_GLOBAL)           fixed-VA global lvalue

template <typename T>
inline const T *Ptr(const void *base, uintptr_t offset) {
    return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(base) + offset);
}
template <typename T>
inline T *Ptr(void *base, uintptr_t offset) {
    return reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(base) + offset);
}
template <typename T>
inline T Read(const void *base, uintptr_t offset) {
    return *Ptr<T>(base, offset);
}
template <typename T>
inline T &Ref(void *base, uintptr_t offset) {
    return *Ptr<T>(base, offset);
}
template <typename T>
inline T Read(uintptr_t address) {
    return *reinterpret_cast<const T *>(address);
}
template <typename T>
inline T &Ref(uintptr_t address) {
    return *reinterpret_cast<T *>(address);
}

// --- Engine primitives ------------------------------------------------------

// Resolve a unit token (`"player"`, `"target"`, `"party3"`, `"nameplate1"`,
// …) to its live `CGUnit_C *`, through the engine's own resolver
// (`FUN_RESOLVE_UNIT_TOKEN`). Returns null for a valid token that currently
// has no unit (e.g. `"target"` with nothing targeted). NOTE: the engine
// RAISES a Lua error for a string that isn't a valid token form (an arbitrary
// name like `"Bob"`), so only pass real unit tokens — the same contract every
// stock `Unit*` global carries. Centralizes the
// `reinterpret_cast<…>(FUN_RESOLVE_UNIT_TOKEN)` idiom that was copy-pasted
// across a dozen modules.
void *ResolveUnitToken(const char *token);

using FrameScript_Initialize_t = bool(__fastcall *)();
using LoadScriptFunctions_t = void(__fastcall *)();

// Master glue Lua-state init function. `__stdcall` (no args, no return).
// Hooked to inject our own glue-side globals after the engine finishes
// registering its 109 glue functions. See FUN_LOAD_GLUE_SCRIPT_FUNCTIONS.
using LoadGlueScriptFunctions_t = void(__stdcall *)();

// --- API documentation descriptors -----------------------------------------
//
// Every Lua registration (see the `Register*` functions in `Game::Lua`) can
// carry a descriptor: the signature the function presents to Lua — typed
// arguments and returns, nilability, defaults — plus a one-sentence summary.
// The descriptors are the single source of truth for the API's SHAPE.
// `src/api/Documentation.cpp` records each registration on the first pass of
// each Lua state and exports the result on demand as Blizzard
// `APIDocumentation` tables (`C_APIDocumentation.GetSystems` / `GetSystem`),
// which the embedded addon's `/api` browser renders. Prose beyond one
// sentence stays in the docs; a descriptor is shape, not manual.
//
// Rules:
//   - One descriptor per REGISTERED NAME, not per C function. The same
//     `Script_*` bound twice with different return conventions
//     (`IsUsableSpell` → 1/nil pairs, `C_Spell.IsSpellUsable` → booleans)
//     gets two descriptors; a pair with the same shape may share one.
//   - `type` uses Blizzard's spellings — "number", "string", "bool",
//     "table", "luaIndex", "UnitToken", "SpellIdentifier", … — or the Name
//     of a `Structure` / enumeration declared elsewhere, which the browser
//     renders as a link.
//   - `Function::system` names the System a GLOBAL belongs to; convention
//     `<Area>Globals` ("SpellGlobals", "ItemGlobals"). Table functions
//     derive theirs from the table ("C_Spell" → System "Spell", Namespace
//     "C_Spell"); frame methods from the registry. A global's system must
//     not reuse a namespaced System's Name — the browser would prefix it.
//   - Descriptors are `const` objects with constant initializers (string
//     literals + file-scope arrays): no heap, no dynamic init.
//   - Summaries are user-facing text: one sentence, plain words, present
//     tense, no patch versions.
namespace Doc {

struct Field {
    const char *name;
    const char *type;
    bool nilable;
    const char *defaultValue; // Lua literal ("false", "0", "\"player\""), or nullptr
    const char *doc;          // one sentence, or nullptr
};

// Required argument / always-present return.
constexpr Field Req(const char *name, const char *type, const char *doc = nullptr) {
    return {name, type, false, nullptr, doc};
}
// Optional: nilable when no default is given, otherwise non-nil with that
// default (Blizzard marks defaulted arguments `Nilable = false` + `Default`).
constexpr Field Opt(const char *name, const char *type, const char *dflt = nullptr,
                    const char *doc = nullptr) {
    return {name, type, dflt == nullptr, dflt, doc};
}
// Variable arguments / returns.
constexpr Field Vararg(const char *type, const char *doc = nullptr) {
    return {"...", type, true, nullptr, doc};
}

// A view over a file-scope `const Field[]`; `{}` means none.
struct FieldList {
    const Field *data = nullptr;
    int count = 0;
    constexpr FieldList() = default;
    template <size_t N>
    constexpr FieldList(const Field (&a)[N]) : data(a), count(static_cast<int>(N)) {}
};

// A registered function. Positional init: `{summary, args, rets}` for a
// table function, `{summary, args, rets, "XGlobals"}` for a global, and
// `{summary, args, rets, nullptr, true}` for a namespaced ClassicAPI
// extension.
struct Function {
    const char *summary;
    FieldList args;
    FieldList rets;
    const char *system;  // globals / glue / aliases only; see the rules above
    bool extension;      // a ClassicAPI-original API, not a Blizzard one
};

// Pairs a frame method with its descriptor BY NAME (the engine's method
// tables are name-keyed; `SetShown` exists on four registries with four
// function pointers). Order is free; unmatched names are reported.
struct Method {
    const char *name;
    const Function *doc;
};

// A table shape a function returns or accepts → `Tables{Type="Structure"}`
// in `system`. Self-registering: nothing binds a structure to Lua, so it
// chains onto a static list at construction. Declare it next to the
// function that returns it.
struct Structure {
    Structure(const char *name, const char *system, FieldList fields,
              const char *summary = nullptr);
    const char *name;
    const char *system;
    FieldList fields;
    const char *summary;
    const Structure *next;
};

// A custom event's payload, attached to its `Event::Custom::AutoReserve`.
struct Event {
    const char *system;
    const char *summary;
    FieldList payload;
};

} // namespace Doc

namespace Lua {
using CFunction = int(__fastcall *)(void *L);

// Lua 5.0 pseudo-index used to read/write entries on the globals table.
constexpr int GLOBALS_INDEX = -10001;
// Lua 5.0 pseudo-index for the registry table — a Lua-state-local
// table protected from script code, used by C modules to anchor
// values across calls (e.g. timer callbacks pinned against GC).
constexpr int REGISTRY_INDEX = -10000;
// LUA_UPVALUEINDEX(i) — pseudo-index for accessing the i-th upvalue
// of a C closure. Lua 5.0 layout: `LUA_GLOBALSINDEX - i`.
constexpr int UpvalueIndex(int i) { return GLOBALS_INDEX - i; }

// `lua_call` / `lua_pcall` nresults sentinel meaning "all".
constexpr int MULTRET = -1;

// Type tag values returned by `Type()` (lua_type).
constexpr int TYPE_NIL = 0;
constexpr int TYPE_BOOLEAN = 1;
constexpr int TYPE_LIGHTUSERDATA = 2;
constexpr int TYPE_NUMBER = 3;
constexpr int TYPE_STRING = 4;
constexpr int TYPE_TABLE = 5;
constexpr int TYPE_FUNCTION = 6;
constexpr int TYPE_USERDATA = 7;
constexpr int TYPE_THREAD = 8;

using lua_isnumber_t = bool(__fastcall *)(void *L, int index);
using lua_isstring_t = bool(__fastcall *)(void *L, int index);
using lua_tonumber_t = double(__fastcall *)(void *L, int index);
using lua_toboolean_t = int(__fastcall *)(void *L, int index);
using lua_tostring_t = const char *(__fastcall *)(void *L, int index);
using lua_strlen_t = unsigned int(__fastcall *)(void *L, int index);
using lua_pushnumber_t = void(__fastcall *)(void *L, double n);
using lua_pushnil_t = void(__fastcall *)(void *L);
using lua_pushboolean_t = void(__fastcall *)(void *L, int b);
using lua_pushstring_t = void(__fastcall *)(void *L, const char *s);
using lua_pushlstring_t = void(__fastcall *)(void *L, const char *s, unsigned int len);
using lua_pushvalue_t = void(__fastcall *)(void *L, int idx);
using lua_pushcclosure_t = void(__fastcall *)(void *L, CFunction fn, int upvals);
using lua_newtable_t = void(__fastcall *)(void *L);
using lua_gettable_t = void(__fastcall *)(void *L, int idx);
using lua_rawget_t = void(__fastcall *)(void *L, int idx);
using lua_settable_t = void(__fastcall *)(void *L, int idx);
using lua_rawset_t = void(__fastcall *)(void *L, int idx);
// Pops the table at the top and installs it as the metatable of the value
// at idx; returns 1.
using lua_setmetatable_t = int(__fastcall *)(void *L, int idx);
using lua_insert_t = void(__fastcall *)(void *L, int idx);
using lua_remove_t = void(__fastcall *)(void *L, int idx);
using lua_gettop_t = int(__fastcall *)(void *L);
using lua_settop_t = void(__fastcall *)(void *L, int idx);
using lua_call_t = void(__fastcall *)(void *L, int nargs, int nresults);
using lua_pcall_t = int(__fastcall *)(void *L, int nargs, int nresults, int errfunc);
using lua_next_t = int(__fastcall *)(void *L, int idx);
using lua_type_t = int(__fastcall *)(void *L, int index);
// The 1.12 `lua_error` wrapper at `0x6F4940` is actually variadic —
// it does `lua_pushvfstring(L, fmt, args)` then prepends `luaL_where`
// and throws. Existing callers pass a single literal (no `%`), which
// is fine because cdecl tolerates extra-arg-less calls. New callers
// forwarding user-supplied error strings should use `Error(L, "%s",
// userMsg)` to avoid format-string interpretation of `%` in the
// message body.
using lua_error_t = void(__cdecl *)(void *L, const char *fmt, ...);
using lua_topointer_t = const void *(__fastcall *)(void *L, int idx);
using lua_tothread_t = void *(__fastcall *)(void *L, int idx);
using lua_iscfunction_t = int(__fastcall *)(void *L, int idx);
using lua_xmove_t = void(__fastcall *)(void *from, void *to, int n);
// `lua_newthread` returns the new `lua_State *` in eax, but Ghidra
// infers `void` because the inner call's value just flows through.
// We declare the return type honestly here; callers that don't want
// to rely on the calling convention preserving eax can read the
// thread back from L's top via `ToPointer(L, -1)`.
using lua_newthread_t = void *(__fastcall *)(void *L);
using lua_resume_t = int(__fastcall *)(void *L, int nargs);
using lua_yield_t = int(__fastcall *)(void *L, int nresults);
using luaL_argerror_t = void(__fastcall *)(void *L, int narg, const char *msg);
using luaL_setn_t = void(__fastcall *)(void *L, int t, int n);
using lua_checkstack_t = int(__fastcall *)(void *L, int size);

extern const lua_isnumber_t IsNumber;
extern const lua_isstring_t IsString;
extern const lua_tonumber_t ToNumber;
extern const lua_toboolean_t ToBoolean;
extern const lua_tostring_t ToString;
extern const lua_strlen_t StrLen;
extern const lua_pushnumber_t PushNumber;
extern const lua_pushnil_t PushNil;
extern const lua_pushboolean_t PushBoolean;
// Convenience overload — takes a real `bool` so callers don't have to
// `static_cast<int>(...)` or `condition ? 1 : 0` at every site. The
// engine's `lua_pushboolean` ABI wants `int` (any non-zero = true), so
// this just funnels through the raw binding with a single conversion.
inline void PushBool(void *L, bool b) { PushBoolean(L, static_cast<int>(b)); }
extern const lua_pushstring_t PushString;
extern const lua_pushlstring_t PushLString;
extern const lua_pushvalue_t PushValue;
extern const lua_pushcclosure_t PushCClosure;
extern const lua_newtable_t NewTable;
extern const lua_gettable_t GetTable;
extern const lua_rawget_t RawGet;
extern const lua_settable_t SetTable;
extern const lua_rawset_t RawSet;
extern const lua_setmetatable_t SetMetatable;
extern const lua_insert_t Insert;
extern const lua_remove_t Remove;
extern const lua_gettop_t GetTop;
extern const lua_settop_t SetTop;
extern const lua_call_t Call;
extern const lua_pcall_t PCall;
extern const lua_next_t Next;
extern const lua_type_t Type;
extern const lua_error_t Error;
extern const lua_topointer_t ToPointer;
extern const lua_tothread_t ToThread;
extern const lua_iscfunction_t IsCFunction;
extern const lua_xmove_t XMove;
extern const lua_newthread_t NewThread;
extern const lua_resume_t Resume;
extern const lua_yield_t Yield;
extern const luaL_argerror_t ArgError;
extern const luaL_setn_t SetN;
extern const lua_checkstack_t CheckStack;

// Returns the global `lua_State *` (read on demand from the engine's global).
// Callable outside a Lua callback, e.g. during LoadScriptFunctions setup.
void *State();

// Resolves a Lua-side frame/object table at stack index `idx` to its
// underlying `CFrameScriptObject *`. Mirrors the standard prologue
// of the engine's own `Set*` frame methods — pushes the object via
// `FrameScript_PushObject`, reads it back as a pointer via
// `FrameScript_GetObject`, then balances the stack. Returns nullptr
// if the slot isn't a CObject (table, light-userdata, or userdata
// types are accepted by the engine resolver). Callers pass `1` for
// the `self` slot of any `frame:method(...)` invocation.
void *ResolveObject(void *L, int idx);

// Same resolve, then the type gate every engine frame method applies to its
// `self` before touching it: `lua_type == table`, resolve to the object, then
// the `IsA(typeId)` vmethod at vtable+0x10. `ResolveObject` alone is not
// enough for a method that casts the result — it hands back a
// `CFrameScriptObject *` for ANY frame, so `GameTooltip.SetSpellByID(button)`
// would reach the tooltip builder with a Button. Frame methods are ordinary
// first-class Lua values (the per-type dispatcher is an `__index` handler
// that returns the method closure), so that call is reachable from plain Lua
// and the gate is not optional.
//
// `typeIdVar` is the class's `VAR_*_LUA_TYPE_ID`, which must match the
// registry the method was registered on. The id is assigned lazily by
// whichever method of the class runs first, so these mirror that assignment
// and work even before any stock method of the type has been called.
//
// Raises the engine's own three errors and returns null. Pass
// `raiseError = false` for an optional object ARGUMENT, where the caller
// wants a silent null rather than a "this" error naming the wrong slot.
// Returns null either way, so callers always check.
void *ResolveTypedObject(void *L, int idx, uintptr_t typeIdVar,
                         bool raiseError = true);

// For a method registered on SEVERAL registries, where any of those types is
// a legitimate `self`. Accepts the object if `IsA` passes for any of the
// listed ids — which is exactly what being in all of those registries means,
// and avoids having to pin down the shared base class.
void *ResolveTypedObjectAny(void *L, int idx, const uintptr_t *typeIdVars,
                            int count, bool raiseError = true);

// The class's live type id, assigning it if the engine has not yet. Needed
// directly only when calling a type-specific vtable slot.
int FrameScriptTypeId(uintptr_t typeIdVar);

// Per-type sugar over `ResolveTypedObject`, so a call site cannot pair a
// method with the wrong class's id. `idx` defaults to the `self` slot.
void *ResolveTooltip(void *L, int idx = 1, bool raiseError = true);
void *ResolveFrame(void *L, int idx = 1, bool raiseError = true);
void *ResolveRegion(void *L, int idx = 1, bool raiseError = true);
void *ResolveTexture(void *L, int idx = 1, bool raiseError = true);
void *ResolveFontString(void *L, int idx = 1, bool raiseError = true);
void *ResolveEditBox(void *L, int idx = 1, bool raiseError = true);
void *ResolveModel(void *L, int idx = 1, bool raiseError = true);

// Every name these registrars bind is ALSO bound under `_G.ClassicAPI` —
// `ClassicAPI.GetSpellInfo`, `ClassicAPI.C_Item.IsBound` — as an escape
// hatch for names something else replaces later, and by value so
// `ClassicAPI.X == X` until that happens. Automatic; a new module needs no
// extra call. See `MirrorRegistration` in Game.cpp for why it lives here
// and what it does not cover.
//
// Every registrar also RECORDS the registration for the API documentation
// (`src/api/Documentation.cpp`): kind, table, name, registry, and which Lua
// state's pass it ran in (in-game / glue / both). Only the FIRST pass of
// each state records — later passes (every `/reload`) are no-ops — and a
// registration made outside a `Run*Registrations` pass is not recorded at
// all. The trailing `doc` argument is the descriptor (see `Game::Doc`
// above); pass `nullptr` for a not-yet-documented registration and
// `_classicapi_UndocumentedAPI()` lists it. The defaults exist only until
// the sweep finishes; then they go and a missing descriptor stops compiling.

// Registers a single global Lua function (e.g. `GetSpellInfo`). The function
// must use the WoW Lua C function ABI: `int __fastcall(void *L)`.
void RegisterGlobalFunction(const char *name, CFunction func,
                            const Doc::Function *doc = nullptr);

// Glue-state equivalent of `RegisterGlobalFunction`. Identical wire
// (calls `FrameScript_RegisterFunction`); the engine routes the
// registration to whichever Lua state `VAR_LUA_STATE` currently
// references. Only safe to call from inside a `GlueModuleAutoRegister`
// callback — at that point the engine has just finished registering
// its own 109 glue functions, and `VAR_LUA_STATE` still points at the
// glue state. Calling outside that window would silently target the
// wrong state.
void RegisterGlueFunction(const char *name, CFunction func,
                          const Doc::Function *doc = nullptr);

// Frame-method registration entry: { name, func } pairs walked by the engine's
// per-frame-type method-table iterator. Layout matches what the engine expects
// natively — name first, then function pointer.
struct FrameMethodEntry {
    const char *name;
    CFunction func;
};

// Registers a batch of methods on a per-frame-type registry (e.g.
// GameTooltipMethodRegistry for `tooltip:Foo()` calls). `context` is the
// registry address — see Offsets::VAR_*_METHOD_REGISTRY. `docs` pairs
// descriptors to the entries by NAME (any order, any subset).
void RegisterFrameMethods(void *context, const FrameMethodEntry *table, int count,
                          const Doc::Method *docs = nullptr, int docCount = 0);

// Registers `func` at `_G[tableName][methodName]`, creating the namespace
// table if it doesn't already exist. This is how modern WoW C_*-style APIs
// are bound — the engine has no built-in support for table-bound Lua
// functions, so we manipulate the globals table directly via the Lua C API.
void RegisterTableFunction(const char *tableName, const char *methodName,
                           CFunction func, const Doc::Function *doc = nullptr);

// Binds `_G[alias]` to the SAME closure as `_G[tableName][methodName]`, by
// value — the way the engine's own Lua-init snippet binds its short aliases
// (`tinsert = table.insert`, `strlen = string.len`, …). Use it after
// registering over a library function whose alias the snippet has already
// captured, so the alias follows the replacement and `alias == table.method`
// stays true. Mirrored under `_G.ClassicAPI` like every other registration.
void RegisterGlobalAlias(const char *alias, const char *tableName, const char *methodName,
                         const Doc::Function *doc = nullptr);

// Key/value pair for `RegisterIntegerEnum`. `key` becomes a field name
// (PascalCase, matching Blizzard's `Enum.*` naming) and `value` is the
// integer the enum field maps to.
struct EnumIntegerEntry {
    const char *key;
    int value;
};

// Registers `_G[parent][sub] = { entries }` as an integer-valued enum
// table, creating `_G[parent]` if needed. Used for Blizzard-style
// `Enum.AddOnSecurityStatus = { Secure=0, Insecure=1, ... }` shapes.
// `docSystem` is the documentation System the enumeration belongs to
// ("SpellBook", "Item", …); the entries themselves are the documentation.
void RegisterIntegerEnum(const char *parent, const char *sub,
                         const EnumIntegerEntry *entries, int count,
                         const char *docSystem = nullptr);

// Set `t[key] = value` on the table currently at stack[-1] (the most
// common shape used when populating a struct-style table mid-build).
// Pushes the key + value, calls `SetTable(L, -3)`, which pops both and
// leaves the table on the stack — so it's safe to chain. NULL string
// values are coerced to `""` so callers don't have to gate each push.
void SetFieldNumber(void *L, const char *key, double value);
void SetFieldString(void *L, const char *key, const char *value);
void SetFieldBool(void *L, const char *key, bool value);

// Sets `_G[name] = value` via `lua_rawset` on the globals
// pseudo-index. `RawSet` over `SetTable` because the globals table has
// no `__newindex` in vanilla 1.12 — explicit intent and skips a dead
// metatable check. Used for flat constant globals like
// `CLASSIC_API_VERSION` and the `LE_ITEM_QUALITY_*` family.
void SetGlobalNumber(void *L, const char *name, double value);

// Pushes `_G[globalName]` onto the stack if it resolves to a string
// (Blizzard's FrameXML globals like `ITEMS_EQUIPPED`, addon-defined
// localization tables, etc.); otherwise pushes `fallback`. Either way
// leaves exactly one string at the top of the stack. The canonical
// pattern is "look up a localized format string with an English
// fallback for stripped servers / pre-init states":
//
//   Game::Lua::PushLocalizedString(L, "ITEMS_EQUIPPED", "%d equipped");
//   Game::Lua::PushNumber(L, count);
//   // … then string.format / sprintf-style consume
void PushLocalizedString(void *L, const char *globalName, const char *fallback);

// Pushes `string.format(_G[globalName] or fallback, n)` — the result
// of formatting a single integer into a localized string. Leaves the
// formatted string on top of the stack. Common shape: most
// FrameXML count-style strings (`ITEMS_EQUIPPED = "%d equipped"`,
// `ITEM_SLOTS_IGNORED = "%d slot(s) ignored"`, etc.). Callers that
// need string or float args, or multiple args, can build their own
// using `PushLocalizedString` + manual `string.format` invocation.
void PushLocalizedFormatInt(void *L, const char *globalName,
                            const char *fallback, int n);

// Pushes `_G[name]` and returns true only if it resolved to a function
// (leaving it on the stack, ready to be called); otherwise pops it and
// returns false. The building block for the CallGlobal* helpers below and for
// any C module that dispatches to a Lua/FrameXML global by name (a pattern
// several modules otherwise open-code).
bool PushGlobalFunction(void *L, const char *name);

// `_G[name]()` — no args, no results; a no-op if the global isn't a function.
// Stack-neutral.
void CallGlobal(void *L, const char *name);

// `_G[name](arg)` — one string arg (NULL → nil), no results. Returns true iff
// the global was a function and got called, so callers can fall back to a
// native path when the FrameXML/addon global is absent. Stack-neutral.
bool CallGlobalString(void *L, const char *name, const char *arg);
} // namespace Lua

// Developer-console command system — the `~` console available when the
// client is launched with `-console`. Vanilla maintains a
// `TSExplicitList<CONSOLECOMMAND>`; these wrap the engine's registrar
// and output paths. See the "Console commands & Interface file export"
// section in CLAUDE.md for the underlying offsets and ABI.
namespace Console {

// Console-command handler ABI: `args` is the command-line text after
// the command name (empty string when invoked bare). The engine
// ignores the return value; return 1 by convention.
using CommandHandler = int(__fastcall *)(void *unused, const char *args);

// Help-grouping categories — the engine's `help` enumerates these nine
// (in this order). Cosmetic: the value only affects which group `help`
// lists the command under; the engine doesn't validate it. Values are
// sequential table indices (graphics=1 confirmed via FUN_0066F6C0, the
// rest follow the displayed order).
enum Category {
    CATEGORY_DEBUG = 0,
    CATEGORY_GRAPHICS = 1,
    CATEGORY_CONSOLE = 2,
    CATEGORY_COMBAT = 3,
    CATEGORY_GAME = 4,
    CATEGORY_DEFAULT = 5,
    CATEGORY_NET = 6,
    CATEGORY_SOUND = 7,
    CATEGORY_GM = 8,
};

// Registers a console command. `name` and `description` are stored BY
// POINTER (the engine doesn't copy them), so pass string literals or
// otherwise process-lifetime storage. Dedup-safe: re-registering the
// same name is a harmless no-op, so it's fine to call from a hook that
// fires more than once. Safe any time after engine boot — the command
// table self-initializes on first use. `description` may be nullptr.
void RegisterCommand(const char *name, CommandHandler handler, int category,
                     const char *description);

// Writes a line to the console output buffer. No-ops cleanly when the
// console isn't active, so it's always safe to call.
void Write(const char *line);

} // namespace Console

// Self-registration for API modules. Each module .cpp declares a file-scope
// `static const Game::ModuleAutoRegister _r{&RegisterLuaFunctions};`, which
// chains itself onto a global list at DLL-load time. `RunModuleRegistrations`
// is called once from the LoadScriptFunctions post-hook to fire them all,
// so DllMain.cpp doesn't need to know the modules exist.
//
// Order is unspecified (LIFO of static-init order across TUs). Modules must
// not depend on each other's registration side effects.
struct ModuleAutoRegister {
    using Fn = void (*)();
    explicit ModuleAutoRegister(Fn fn);
    Fn fn;
    ModuleAutoRegister *next;
};

void RunModuleRegistrations();

// Glue-state mirror of `ModuleAutoRegister`. Modules wanting login-
// screen exposure declare a file-scope
// `static const Game::GlueModuleAutoRegister _g{&RegisterGlueFunctions};`
// alongside (or instead of) the in-game `ModuleAutoRegister`. Glue
// callbacks run from the `FUN_LOAD_GLUE_SCRIPT_FUNCTIONS` post-hook,
// once per glue boot (game launch + every world→glue return). Use
// `Game::Lua::RegisterGlueFunction` from inside the callback.
struct GlueModuleAutoRegister {
    using Fn = void (*)();
    explicit GlueModuleAutoRegister(Fn fn);
    Fn fn;
    GlueModuleAutoRegister *next;
};

void RunGlueModuleRegistrations();

// Declarative MinHook registration. Each feature module declares a
// file-scope `static const Game::HookAutoRegister _hookreg{target,
// &hook_fn, reinterpret_cast<void**>(&original_fn)};` and the installer
// (`InstallHooks` in DllMain.cpp) walks the list once after
// `MH_Initialize`, creating each hook and QUEUE-enabling it; a single
// `MH_ApplyQueued` then activates them all in one thread-freeze.
//
// The install runs OFF the loader lock (via the `Load` export or the
// fallback worker thread — see DllMain.cpp), never inside DllMain, so
// the thread-freeze can't deadlock or stall the loader.
//
// Same lifetime rules as ModuleAutoRegister: constructors chain onto
// a static-init list before DllMain runs, the linker keeps the OBJ
// because the constructor has side effects, and order across TUs is
// undefined but doesn't matter here (hooks are independent).
//
// Use only for feature hooks. The four core engine-init hooks in
// DllMain (FrameScript_Initialize / LoadScriptFunctions /
// LoadGlueScriptFunctions / Frame::RegisterEvent) have inline logic that
// interleaves with the hook chain and stays with the installer.
struct HookAutoRegister {
    HookAutoRegister(uintptr_t target, void *hook, void **original);
    uintptr_t target;
    void *hook;
    void **original;
    HookAutoRegister *next;
};

// Creates and QUEUE-enables every registered hook (no `MH_ApplyQueued`
// here — the caller applies the whole batch at once). Returns `false`
// and stops on first failure.
bool RunHookRegistrations();

// Self-registration for per-/reload state cleanup. A module that keeps
// file-static state which goes STALE across a game /reload declares a
// file-scope `static const Game::ReloadAutoRegister _reload{&PrepareForReload};`
// right after its `void PrepareForReload()`. `RunReloadCleanups()` — called
// from the FrameScript_Initialize hook, BEFORE the engine tears down the Lua
// state — fires them all, so DllMain no longer hand-maintains the list and a
// new module can't forget to wire itself in.
//
// State is "reload-fragile" when it survives in C++ but its meaning does not:
//   - Lua registry refs / handler cells (the Lua reset invalidates them);
//   - maps/sets keyed by a frame/object POINTER (the allocator recycles
//     addresses, so an old entry aliases an unrelated new object);
//   - cached event-slot indices (the engine rebuilds the event table).
// Firing a stale entry is the recurring bug this prevents (Tooltip::SetEvents
// issue #33, Frame::Attributes). A pure-C++ cache keyed by spellID/itemID is
// NOT fragile and must not register.
//
// Same lifetime + ordering rules as ModuleAutoRegister: static-init chaining,
// undefined cross-TU order — fine, because each callback only clears its OWN
// state. This is the "clear stale state" category only; a save-before-teardown
// like Player::NameCache::Flush stays an explicit DllMain call (it persists
// rather than clears, and also runs on the glue-return path).
struct ReloadAutoRegister {
    using Fn = void (*)();
    explicit ReloadAutoRegister(Fn fn);
    Fn fn;
    ReloadAutoRegister *next;
};

void RunReloadCleanups();

} // namespace Game
