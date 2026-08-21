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
// globals (so handlers reading `this`/`arg1` keep working), but push `self` +
// `arg1..argN` as real arguments and `pcall` with `nargs = 1 + N`. The
// reimplementation mirrors the engine's message-handler errfunc, but saves the
// clobbered globals on the Lua stack (not registry refs like the engine) so an
// out-of-memory longjmp mid-push unwinds them for free — no leaked refs, no
// per-call ref churn. See RunModern.
//
// The reimplemented path is taken ONLY for a handler that actually declares a
// parameter or is vararg (checked via its Proto — see HandlerWantsArgs). A
// handler that declares NO parameters can't observe positional args, so pushing
// them is a no-op — those route through the original engine runner unchanged:
// provably identical to vanilla, and OFF the engine's hottest Lua path (the vast
// majority of vanilla handlers, which read `this`/`arg1`, are param-less). So a
// param-less vanilla handler is untouched; only a param-declaring handler sees
// the `(self, [event,] arg1..argN)` shape.
//
// Irreducible caveat: a param-declaring handler and a modern handler are
// indistinguishable to the dispatcher — both are one-param Lua functions. So a
// vanilla handler that declares a parameter expecting it to be nil (e.g. a
// function reused as both a direct call and a handler) WILL now receive a real
// value. There is no signal to tell the two apart; such a handler must opt out
// per-session via `SetModernScriptArgs(false)`.
//
// Convention: `(self, arg1..argN)` for every script, plus `event` inserted as
// the first positional for OnEvent — `(self, event, arg1..argN)` — matching
// retail (and Frame::Modern's HookScript). An OnEvent dispatch is detected
// structurally at the runner: the handler ref being invoked equals the frame's
// OnEvent slot handler (frame+OFF_FRAME_ONEVENT_SLOT). This is exact — each
// SetScript creates a distinct ref, so a function bound to two scripts still
// differs per slot — so no fragile "are we mid event-dispatch" flag is needed.
//
// Runtime switch, default ON — modern handler signatures are a core 5.1
// feature, so ports that write `function(self, delta)` work out of the box.
// `SetModernScriptArgs(false)` disables it, `GetModernScriptArgs()` reads the
// state. While disabled both detours are a straight passthrough to the original
// runner (exact vanilla behavior, zero reimplementation risk) — the OFF escape
// valve, since this lands on the hottest Lua path in the engine
// (FUN_FRAME_RUN_SCRIPT_ARGS fires for every OnUpdate frame).

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::ScriptArgs {

namespace {

// Runtime switch. Default ON (core 5.1 behavior). When false, both detours
// tail-call the original runner unchanged (exact vanilla behavior, no
// reimplementation risk) — the OFF escape valve for this hot path.
bool g_enabled = true;

// Engine cap: the runner stops setting arg globals at index 19 (arg1..arg19).
constexpr int kMaxArgs = 19;

// --- raw engine primitives not exposed via Game::Lua -----------------------
// lua_rawgeti(L, idx, n) — push table_at_idx[n]. Used with idx = REGISTRY to
// push a value stored under an integer ref (handler / frame object / message
// handler / luaL_ref'd saved globals).
using RawGetI_t = void(__fastcall *)(void *L, int idx, int ref);
// FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER — __thiscall(frame, nameOrNull);
// lazily materializes the frame's Lua-registry ref at frame+OFF_COBJECT_LUA_REF.
using ScriptRegister_t = void(__thiscall *)(void *frame, void *nameOrNull);

const auto RawGetI = reinterpret_cast<RawGetI_t>(Offsets::LUA_RAWGETI);

// Returns the frame's Lua object ref (frame+0x08), lazily creating it exactly
// as the engine runner does (`if (refcount == 0) ScriptRegister(frame, 0)`).
int EnsureLuaRef(void *frame) {
    if (Game::Read<int>(frame, Offsets::OFF_COBJECT_LUA_REFCOUNT) == 0)
        reinterpret_cast<ScriptRegister_t>(
            Offsets::FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER)(frame, nullptr);
    return Game::Read<int>(frame, Offsets::OFF_COBJECT_LUA_REF);
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

// One parsed positional argument from the runner's printf-style format.
// Numbers ('d'/'u'/'f') land in `num`; strings ('s') keep the char* the engine
// handed us (stable for the duration of the call).
struct Arg {
    char name[6]; // "arg1".."arg19"
    char kind;    // 'd' | 'u' | 'f' | 's'
    double num;
    const char *str;
};

// Decodes `fmt`/`vaPtr` (the runner's %d/%u/%f/%s specs over a packed vararg
// buffer) into `out`, returning the count (capped at kMaxArgs). Unknown specs
// are skipped and consume no vararg — mirroring the engine runner's switch.
// Pure C, touches no Lua state, so it can never throw.
int ParseArgs(const char *fmt, const void *vaPtr, Arg *out) {
    if (fmt == nullptr)
        return 0;
    const char *cur = static_cast<const char *>(vaPtr);
    int n = 0;
    for (const char *p = fmt; *p != '\0' && n < kMaxArgs; ++p) {
        if (*p != '%')
            continue;
        ++p;
        if (*p == '\0')
            break;
        Arg &a = out[n];
        switch (*p) {
        case 'd':
            a.num = static_cast<double>(*reinterpret_cast<const int *>(cur));
            cur += 4;
            break;
        case 'u':
            a.num = static_cast<double>(
                *reinterpret_cast<const unsigned int *>(cur));
            cur += 4;
            break;
        case 'f':
            a.num = *reinterpret_cast<const double *>(cur);
            cur += 8;
            break;
        case 's':
            a.str = *reinterpret_cast<const char *const *>(cur);
            cur += 4;
            break;
        default:
            continue; // unknown spec — consume nothing
        }
        a.kind = *p;
        ArgName(a.name, n + 1);
        ++n;
    }
    return n;
}

// Pushes a parsed argument's value onto the Lua stack.
void PushArg(void *L, const Arg &a) {
    if (a.kind == 's')
        Game::Lua::PushString(L, a.str);
    else
        Game::Lua::PushNumber(L, a.num);
}

// Reimplements the runner tail: set the `this`/`arg1..argN` globals, invoke the
// handler with modern positional args `(self, [event,] arg1..argN)` under the
// engine's own message-handler errfunc, then restore the globals. `fmt`/`vaPtr`
// are null for the no-arg runner (N == 0).
//
// Unlike the engine (and our first cut), the old global values are saved on the
// Lua STACK, not in the registry via luaL_ref. A registry save leaks its ref if
// any push before the protected call longjmps — and out-of-memory, the one
// failure that actually fires here, does exactly that. Stack slots need no
// cleanup: an unwinding error resets `L->top` past them for free, so nothing
// leaks and there's no per-call ref churn on the OnUpdate hot path. The cost is
// a deeper stack (up to ~2*N slots live at once), so we CheckStack up front.
//
// Returns false WITHOUT side effects when it declines (no Lua state, or the
// stack can't grow); the caller then runs the original. Every false path is
// before the first push, so falling back never double-runs the handler.
bool RunModern(int handlerRef, void *frame, const char *fmt, const void *vaPtr) {
    using namespace Game::Lua;
    void *L = State();
    if (L == nullptr)
        return false;
    // Headroom for the saved-old block plus the transient set/call pushes.
    if (CheckStack(L, 2 * kMaxArgs + 16) == 0)
        return false;

    const bool haveThis = frame != nullptr;
    // OnEvent dispatch? The event dispatchers invoke the frame's OnEvent slot
    // (frame+0x0C); matching the handler ref against that slot identifies it
    // exactly. When so, `event` (a global the dispatcher has set) is prepended
    // as the first positional to mirror retail's (self, event, arg1..argN).
    const bool isOnEvent =
        haveThis &&
        Game::Read<int>(frame, Offsets::OFF_FRAME_ONEVENT_SLOT) == handlerRef;
    const int selfRef = haveThis ? EnsureLuaRef(frame) : 0;

    Arg args[kMaxArgs];
    const int n = ParseArgs(fmt, vaPtr, args);

    // --- save the globals we're about to clobber, onto the stack ------------
    // Absolute indices, live until the closing SetTop(base):
    //   base+1                = old _G.this        (if haveThis)
    //   savedArgBase+1 .. +n   = old _G.arg1..argN
    const int base = GetTop(L);
    if (haveThis) {
        PushString(L, "this");
        GetTable(L, GLOBALS_INDEX); // [.. oldThis]
    }
    const int savedArgBase = base + (haveThis ? 1 : 0);
    for (int k = 0; k < n; ++k) {
        PushString(L, args[k].name);
        GetTable(L, GLOBALS_INDEX); // [.. oldArgk]
    }

    // --- set the new globals (each op is stack-neutral) ---------------------
    if (haveThis) {
        PushString(L, "this");
        RawGetI(L, REGISTRY_INDEX, selfRef); // frame object
        SetTable(L, GLOBALS_INDEX);
    }
    for (int k = 0; k < n; ++k) {
        PushString(L, args[k].name);
        PushArg(L, args[k]);
        SetTable(L, GLOBALS_INDEX);
    }

    // --- call handler(self, [event,] arg1..argN) ---------------------------
    const int callBase = GetTop(L); // == base + saved-block size
    const int errRef = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_FRAMESCRIPT_ERROR_HANDLER_REF));
    int errIdx = 0;
    if (errRef > 0) {
        RawGetI(L, REGISTRY_INDEX, errRef); // message handler
        errIdx = callBase + 1;              // absolute index of the errfunc
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
    for (int k = 0; k < n; ++k)
        PushArg(L, args[k]);
    PCall(L, 1 + (isOnEvent ? 1 : 0) + n, 0, errIdx);
    SetTop(L, callBase); // drop the message handler + any leftover error object

    // --- restore globals from the saved block, then drop it -----------------
    for (int k = 0; k < n; ++k) {
        PushString(L, args[k].name);
        PushValue(L, savedArgBase + 1 + k);
        SetTable(L, GLOBALS_INDEX);
    }
    if (haveThis) {
        PushString(L, "this");
        PushValue(L, base + 1);
        SetTable(L, GLOBALS_INDEX);
    }
    SetTop(L, base);
    return true;
}

// Lua 5.0 internal offsets for reading a handler's arity — verified against the
// engine's own luaD_precall (0x006F6050) / lua_dump (0x006F4370) /
// lua_iscfunction (0x006F34A0):
//   lua_State: top @ +0x08 (TValue *, next free slot)
//   TValue:    tag @ +0x00 (int); GC/closure pointer @ +0x08  (16-byte TValue)
//   Closure:   isC byte @ +0x06; union member (C func / Proto) @ +0x0C
//   Proto:     numparams (u8) @ +0x45, is_vararg (u8) @ +0x46
// Single-use here, so kept local (like the lua_State field offsets elsewhere).
constexpr uintptr_t OFF_LUASTATE_TOP = 0x08;
constexpr uintptr_t OFF_TVALUE_GC = 0x08;
constexpr uintptr_t OFF_CLOSURE_ISC = 0x06;
constexpr uintptr_t OFF_LCLOSURE_PROTO = 0x0C;
constexpr uintptr_t OFF_PROTO_NUMPARAMS = 0x45;
constexpr uintptr_t OFF_PROTO_IS_VARARG = 0x46;
constexpr int kLuaTFunction = 6;
constexpr uintptr_t kTValueSize = 0x10;

// Does this handler actually observe positional args? A vanilla handler reads
// this/arg1 globals and declares NO parameters, so pushing positional args to
// it is observably identical to vanilla — those route through the original
// engine runner instead (exact vanilla, and off the hottest Lua path). Only a
// Lua closure that declares a parameter or is vararg — a modern
// `function(self, delta)` / `function(self, ...)` — takes the reimplemented
// positional path. A C-closure handler (HookScript's thunk, say) reads
// globals/upvalues, never positional args, so it counts as "no" too.
//
// Reads the handler's Proto directly (the engine exposes no arity via the C
// API); every read is bounded — a non-function ref, a C closure, or a null
// Proto all fall through to "no".
bool HandlerWantsArgs(void *L, int handlerRef) {
    RawGetI(L, Game::Lua::REGISTRY_INDEX, handlerRef); // push registry[handlerRef]
    bool wants = false;
    const void *tv = reinterpret_cast<const void *>(
        Game::Read<uintptr_t>(L, OFF_LUASTATE_TOP) - kTValueSize);
    if (Game::Read<int>(tv, 0) == kLuaTFunction) {
        const void *cl =
            reinterpret_cast<const void *>(Game::Read<uintptr_t>(tv, OFF_TVALUE_GC));
        if (cl != nullptr && Game::Read<uint8_t>(cl, OFF_CLOSURE_ISC) == 0) { // Lua closure
            const void *proto = reinterpret_cast<const void *>(
                Game::Read<uintptr_t>(cl, OFF_LCLOSURE_PROTO));
            if (proto != nullptr)
                wants = Game::Read<uint8_t>(proto, OFF_PROTO_NUMPARAMS) > 0 ||
                        Game::Read<uint8_t>(proto, OFF_PROTO_IS_VARARG) != 0;
        }
    }
    Game::Lua::SetTop(L, -2); // pop the handler
    return wants;
}

// --- FUN_FRAME_RUN_SCRIPT_ARGS co-hook (scripts with values) ----------------
using RunArgs_t = void(__cdecl *)(int handlerRef, void *frame, const char *fmt,
                                  const void *vaPtr);
RunArgs_t g_origRunArgs = nullptr;

void __cdecl RunArgs_h(int handlerRef, void *frame, const char *fmt,
                       const void *vaPtr) {
    if (g_enabled && handlerRef != 0 && fmt != nullptr) {
        void *L = Game::Lua::State();
        if (L != nullptr && HandlerWantsArgs(L, handlerRef) &&
            RunModern(handlerRef, frame, fmt, vaPtr))
            return;
    }
    g_origRunArgs(handlerRef, frame, fmt, vaPtr);
}

// --- FUN_FRAME_INVOKE_SCRIPT co-hook (no-arg scripts) -----------------------
// Same address Tooltip::SetEvents calls directly to fire OnTooltipSet*; with
// the switch enabled those fire with (self) too, which is the modern shape.
using Invoke_t = void(__fastcall *)(int handlerRef, void *frame);
Invoke_t g_origInvoke = nullptr;

void __fastcall Invoke_h(int handlerRef, void *frame) {
    if (g_enabled && handlerRef != 0 && frame != nullptr) {
        void *L = Game::Lua::State();
        if (L != nullptr && HandlerWantsArgs(L, handlerRef) &&
            RunModern(handlerRef, frame, /*fmt*/ nullptr, /*vaPtr*/ nullptr))
            return;
    }
    g_origInvoke(handlerRef, frame);
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
