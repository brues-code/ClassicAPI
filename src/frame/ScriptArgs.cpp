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

// Modern positional script arguments — make every frame-script handler
// receive `(self, arg1..argN)` the way later clients do, on top of the
// vanilla `this`/`arg1`/`event` globals.
//
//   frame:SetScript("OnMouseWheel", function()            print(this, arg1) end)  -- vanilla
//   frame:SetScript("OnMouseWheel", function(self, delta)  print(self, delta) end)  -- modern
//
// Vanilla ALWAYS invokes a script handler with ZERO Lua arguments: the two
// runners the engine funnels every dispatch through — FUN_FRAME_INVOKE_SCRIPT
// (no-arg scripts) and FUN_FRAME_RUN_SCRIPT_ARGS (scripts with values) — set
// the `this`/`arg1..argN` globals and then `lua_pcall(handler, /*nargs*/0)`.
// So `function(self, delta)` gets nothing today.
//
// We co-hook both runners and REPLACE their pcall tail: keep setting the same
// globals (so vanilla handlers reading `this`/`arg1` keep working — this is
// purely additive; Lua silently drops the extra positional args a zero-param
// handler doesn't declare), but push `self` + `arg1..argN` as real arguments
// and `pcall` with `nargs = 1 + N`. The reimplementation mirrors the engine's
// exact global save/set/restore and its message-handler errfunc; only the
// final push+pcall differs.
//
// Convention: `(self, arg1..argN)` for every script, plus `event` inserted as
// the first positional for OnEvent — `(self, event, arg1..argN)` — matching
// retail (and Frame::Modern's HookScript). An OnEvent dispatch is detected
// structurally at the runner: the handler ref being invoked equals the frame's
// OnEvent slot handler (frame+OFF_FRAME_ONEVENT_SLOT). This is exact — each
// SetScript creates a distinct ref, so a function bound to two scripts still
// differs per slot — so no fragile "are we mid event-dispatch" flag is needed.
//
// Runtime switch, default OFF (opt-in): `SetModernScriptArgs(true)` enables it,
// `GetModernScriptArgs()` reads the state. While disabled both detours are a
// straight passthrough to the original runner (zero reimplementation risk).
// Opt-in because this lands on the hottest Lua path in the engine
// (FUN_FRAME_RUN_SCRIPT_ARGS fires for every OnUpdate frame).

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::ScriptArgs {

namespace {

// Runtime switch. Default OFF (opt-in). When false, both detours tail-call the
// original runner unchanged (exact vanilla behavior, no reimplementation risk).
bool g_enabled = false;

// Engine cap: the runner stops setting arg globals at index 19 (arg1..arg19).
constexpr int kMaxArgs = 19;

// --- raw engine primitives not exposed via Game::Lua -----------------------
// lua_rawgeti(L, idx, n) — push table_at_idx[n]. Used with idx = REGISTRY to
// push a value stored under an integer ref (handler / frame object / message
// handler / luaL_ref'd saved globals).
using RawGetI_t = void(__fastcall *)(void *L, int idx, int ref);
// luaL_ref(L, t) → int — pop the top, store it under a fresh int key in table
// `t`, return the key. luaL_unref frees it.
using LuaLRef_t = int(__fastcall *)(void *L, int t);
using LuaLUnref_t = void(__fastcall *)(void *L, int t, int ref);
// FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER — __thiscall(frame, nameOrNull);
// lazily materializes the frame's Lua-registry ref at frame+OFF_COBJECT_LUA_REF.
using ScriptRegister_t = void(__thiscall *)(void *frame, void *nameOrNull);

const auto RawGetI = reinterpret_cast<RawGetI_t>(Offsets::LUA_RAWGETI);
const auto LuaLRef = reinterpret_cast<LuaLRef_t>(Offsets::LUA_REF_REF);
const auto LuaLUnref = reinterpret_cast<LuaLUnref_t>(Offsets::LUA_REF_UNREF);

// Returns the frame's Lua object ref (frame+0x08), lazily creating it exactly
// as the engine runner does (`if (refcount == 0) ScriptRegister(frame, 0)`).
int EnsureLuaRef(void *frame) {
    auto *f = reinterpret_cast<uint8_t *>(frame);
    if (*reinterpret_cast<int *>(f + Offsets::OFF_COBJECT_LUA_REFCOUNT) == 0)
        reinterpret_cast<ScriptRegister_t>(
            Offsets::FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER)(frame, nullptr);
    return *reinterpret_cast<int *>(f + Offsets::OFF_COBJECT_LUA_REF);
}

// Writes "arg" + k (k in 1..19) into buf; buf must hold at least 6 bytes.
void ArgName(char *buf, int k) {
    buf[0] = 'a';
    buf[1] = 'r';
    buf[2] = 'g';
    if (k < 10) {
        buf[3] = static_cast<char>('0' + k);
        buf[4] = '\0';
    } else {
        buf[3] = static_cast<char>('0' + k / 10);
        buf[4] = static_cast<char>('0' + k % 10);
        buf[5] = '\0';
    }
}

// Sets _G[name] = <value currently on the stack top>, capturing the previous
// _G[name] into *outOldRef (a luaL_ref for later restore). Stack-neutral:
// consumes the value, leaves the stack as it was minus that value.
void SetGlobalSaving(void *L, const char *name, int *outOldRef) {
    using namespace Game::Lua;
    PushString(L, name);        // [.., newVal, name]
    GetTable(L, GLOBALS_INDEX); // [.., newVal, oldVal]
    *outOldRef = LuaLRef(L, REGISTRY_INDEX); // [.., newVal]
    PushString(L, name);        // [.., newVal, name]
    Insert(L, -2);              // [.., name, newVal]
    SetTable(L, GLOBALS_INDEX); // [..]
}

// Restores _G[name] to the value at oldRef, then frees the ref.
void RestoreGlobal(void *L, const char *name, int oldRef) {
    using namespace Game::Lua;
    RawGetI(L, REGISTRY_INDEX, oldRef); // [.., oldVal]
    PushString(L, name);                // [.., oldVal, name]
    Insert(L, -2);                      // [.., name, oldVal]
    SetTable(L, GLOBALS_INDEX);         // [..]
    LuaLUnref(L, REGISTRY_INDEX, oldRef);
}

// Reimplements the runner tail: set globals (with save/restore), then invoke
// the handler with modern positional args `(self, arg1..argN)` under the
// engine's own message-handler errfunc. `fmt`/`vaPtr` are null for the no-arg
// runner (N == 0).
void RunModern(int handlerRef, void *frame, const char *fmt, const void *vaPtr) {
    using namespace Game::Lua;
    void *L = State();
    if (L == nullptr)
        return;

    // --- _G.this = frame -----------------------------------------------------
    const bool haveThis = frame != nullptr;
    // OnEvent dispatch? The event dispatchers invoke the frame's OnEvent slot
    // (frame+0x0C); matching the handler ref against that slot identifies it
    // exactly. When so, `event` (a global the dispatcher has set) is prepended
    // as the first positional to mirror retail's (self, event, arg1..argN).
    const bool isOnEvent =
        haveThis &&
        *reinterpret_cast<const int *>(reinterpret_cast<uint8_t *>(frame) +
                                       Offsets::OFF_FRAME_ONEVENT_SLOT) ==
            handlerRef;
    int selfRef = 0;
    int oldThisRef = 0;
    if (haveThis) {
        selfRef = EnsureLuaRef(frame);
        RawGetI(L, REGISTRY_INDEX, selfRef); // push frame object
        SetGlobalSaving(L, "this", &oldThisRef);
    }

    // --- _G.arg1..argN from the format --------------------------------------
    int oldArgRefs[kMaxArgs];
    char argNames[kMaxArgs][6];
    int n = 0;
    if (fmt != nullptr) {
        const char *cur = static_cast<const char *>(vaPtr);
        for (const char *p = fmt; *p != '\0' && n < kMaxArgs; ++p) {
            if (*p != '%')
                continue;
            ++p;
            if (*p == '\0')
                break;
            bool consumed = true;
            switch (*p) {
            case 'd':
                PushNumber(L, static_cast<double>(*reinterpret_cast<const int *>(cur)));
                cur += 4;
                break;
            case 'u':
                PushNumber(L, static_cast<double>(
                                  *reinterpret_cast<const unsigned int *>(cur)));
                cur += 4;
                break;
            case 'f':
                PushNumber(L, *reinterpret_cast<const double *>(cur));
                cur += 8;
                break;
            case 's':
                PushString(L, *reinterpret_cast<const char *const *>(cur));
                cur += 4;
                break;
            default:
                consumed = false; // unknown spec — skip, consume nothing
                break;
            }
            if (!consumed)
                continue;
            ArgName(argNames[n], n + 1);
            SetGlobalSaving(L, argNames[n], &oldArgRefs[n]);
            ++n;
        }
    }

    // --- call handler(self, arg1..argN) -------------------------------------
    const int base = GetTop(L);
    const int errRef = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_FRAMESCRIPT_ERROR_HANDLER_REF));
    int errIdx = 0;
    if (errRef > 0) {
        RawGetI(L, REGISTRY_INDEX, errRef); // message handler
        errIdx = base + 1;                  // absolute index of the errfunc
    }
    RawGetI(L, REGISTRY_INDEX, handlerRef); // handler
    if (haveThis)
        RawGetI(L, REGISTRY_INDEX, selfRef); // self
    else
        PushNil(L);
    if (isOnEvent) { // event goes before the args, retail-style
        PushString(L, "event");
        GetTable(L, GLOBALS_INDEX);
    }
    for (int k = 0; k < n; ++k) { // positional args, re-read from the globals
        PushString(L, argNames[k]);
        GetTable(L, GLOBALS_INDEX);
    }
    PCall(L, 1 + (isOnEvent ? 1 : 0) + n, 0, errIdx);
    SetTop(L, base); // drop the message handler + any leftover error object

    // --- restore globals (reverse order) ------------------------------------
    for (int k = n - 1; k >= 0; --k)
        RestoreGlobal(L, argNames[k], oldArgRefs[k]);
    if (haveThis)
        RestoreGlobal(L, "this", oldThisRef);
}

// --- FUN_FRAME_RUN_SCRIPT_ARGS co-hook (scripts with values) ----------------
using RunArgs_t = void(__cdecl *)(int handlerRef, void *frame, const char *fmt,
                                  const void *vaPtr);
RunArgs_t g_origRunArgs = nullptr;

void __cdecl RunArgs_h(int handlerRef, void *frame, const char *fmt,
                       const void *vaPtr) {
    if (!g_enabled || handlerRef == 0 || fmt == nullptr) {
        g_origRunArgs(handlerRef, frame, fmt, vaPtr);
        return;
    }
    RunModern(handlerRef, frame, fmt, vaPtr);
}

// --- FUN_FRAME_INVOKE_SCRIPT co-hook (no-arg scripts) -----------------------
// Same address Tooltip::SetEvents calls directly to fire OnTooltipSet*; with
// the switch enabled those fire with (self) too, which is the modern shape.
using Invoke_t = void(__fastcall *)(int handlerRef, void *frame);
Invoke_t g_origInvoke = nullptr;

void __fastcall Invoke_h(int handlerRef, void *frame) {
    if (!g_enabled || handlerRef == 0 || frame == nullptr) {
        g_origInvoke(handlerRef, frame);
        return;
    }
    RunModern(handlerRef, frame, /*fmt*/ nullptr, /*vaPtr*/ nullptr);
}

const Game::HookAutoRegister _runArgsHook{
    Offsets::FUN_FRAME_RUN_SCRIPT_ARGS,
    reinterpret_cast<void *>(&RunArgs_h),
    reinterpret_cast<void **>(&g_origRunArgs)};

const Game::HookAutoRegister _invokeHook{
    Offsets::FUN_FRAME_INVOKE_SCRIPT,
    reinterpret_cast<void *>(&Invoke_h),
    reinterpret_cast<void **>(&g_origInvoke)};

// --- Lua toggle -------------------------------------------------------------
int __fastcall Script_SetModernScriptArgs(void *L) {
    g_enabled = Game::Lua::ToBoolean(L, 1) != 0;
    Game::Lua::PushBool(L, g_enabled);
    return 1;
}

int __fastcall Script_GetModernScriptArgs(void *L) {
    Game::Lua::PushBool(L, g_enabled);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetModernScriptArgs",
                                      &Script_SetModernScriptArgs);
    Game::Lua::RegisterGlobalFunction("GetModernScriptArgs",
                                      &Script_GetModernScriptArgs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::ScriptArgs
