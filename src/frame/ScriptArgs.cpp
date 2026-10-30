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
// We redirect each runner's own lua_pcall (RunnerPCall). By then the runner
// has done everything else — parsed its format, saved and set `this` and
// arg1..argN — so the values are read back from those globals and passed as
// `(self, arg1..argN)` with `nargs = 1 + N`. The format is never parsed here.
// The runner's switch is the only one, so a spec another DLL adds to it at
// runtime keeps working: SuperWoWhook's `%e` (a registry ref) crashed the copy
// of the switch this module used to carry.
//
// Positional args go only to a handler that can observe them: a Lua function
// that declares a parameter or is vararg (checked via its Proto), or a C
// function such as a frame method bound directly — see FunctionWantsArgs. Any
// other handler is called exactly as the engine calls it, so a param-less
// vanilla handler (the vast majority, reading `this`/`arg1`) is untouched.
//
// Each OnEvent handler also gets its own `event` global, saved before and
// restored after it (SaveEvent), as 3.3.5 does per handler.
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
// retail. An OnEvent dispatch is detected
// structurally at the runner: the handler ref being invoked equals the frame's
// OnEvent slot handler (frame+OFF_FRAME_ONEVENT_SLOT). This is exact — each
// SetScript creates a distinct ref, so a function bound to two scripts still
// differs per slot — so no fragile "are we mid event-dispatch" flag is needed.
//
// Runtime switch, default ON — modern handler signatures are a core 5.1
// feature, so ports that write `function(self, delta)` work out of the box.
// `SetModernScriptArgs(false)` disables it, `GetModernScriptArgs()` reads the
// state. While disabled the redirected pcall is the engine's call unchanged —
// exact vanilla behavior.
//
// Scripts the engine never runs (RunWithValues: custom events,
// OnAttributeChanged, …) carry arbitrary Lua values, which no runner format
// can, so RunCore runs those itself, as 3.3.5's FrameScript_Execute does.

#include "frame/ScriptArgs.h"

#include "Common.h"
#include "Game.h"
#include "HookSecureFunc.h"
#include "Offsets.h"
#include "frame/ScriptBindings.h"

#include <cstdint>
#include <cstdio>

namespace Frame::ScriptArgs {

namespace {

// Runtime switch. Default ON (core 5.1 behavior). When false, the redirected
// pcall makes the engine's own call and RunWithValues passes no positional
// args — exact vanilla behavior.
bool g_enabled = true;

// Chained at static-init time by each `AutoObserve` constructor.
AutoObserve *g_observers = nullptr;

void Notify(bool begin, int handlerRef, void *frame) {
    for (AutoObserve *o = g_observers; o != nullptr; o = o->next)
        o->fn(begin, handlerRef, frame);
}

// Chained at static-init time by each `AutoTrailingBool` constructor.
AutoTrailingBool *g_trailing = nullptr;

using Game::Lua::RawGetI;

// Writes "arg" + k into buf.
void ArgName(char (&buf)[16], int k) { std::snprintf(buf, sizeof buf, "arg%d", k); }

// OnEvent dispatch? The event dispatchers invoke the frame's OnEvent slot
// (frame+0x0C); matching the handler ref against that slot identifies it
// exactly.
bool IsOnEvent(int handlerRef, void *frame) {
    return frame != nullptr &&
           Game::Read<int>(frame, Offsets::OFF_FRAME_ONEVENT_SLOT) == handlerRef;
}

// Keeps the `event` global per OnEvent handler, as 3.3.5's
// FrameScript_Execute does: saved before the handler, restored after it. The
// engine dispatchers (0x703E50 / 0x703F50) set `event` once for the whole
// dispatch, so a handler that assigns the global would otherwise change it
// for every listener after it. SaveEvent pushes the value and returns its
// stack index (0 when there is nothing to keep); RestoreEvent puts it back and
// pops it. Every run between them is stack-balanced.
int SaveEvent(void *L, int handlerRef, void *frame) {
    using namespace Game::Lua;
    if (!g_enabled || L == nullptr || !IsOnEvent(handlerRef, frame) ||
        CheckStack(L, 2) == 0)
        return 0;
    PushString(L, "event");
    RawGet(L, GLOBALS_INDEX);
    return GetTop(L);
}

void RestoreEvent(void *L, int saved) {
    using namespace Game::Lua;
    if (saved == 0)
        return;
    PushString(L, "event");
    PushValue(L, saved);
    SetTable(L, GLOBALS_INDEX);
    SetTop(L, saved - 1);
}

// Runs a handler with the `n` Lua values at absolute index `firstArgIdx` (they
// sit below everything this pushes, so the index holds): set the
// `this`/`arg1..argN` globals, invoke the handler with modern positional args
// `(self, [event,] arg1..argN)` under the engine's own message-handler
// errfunc, then restore the globals.
//
// Unlike the engine runners, the old global values are saved on the Lua
// STACK, not in the registry via luaL_ref. A registry save leaks its ref if
// any push before the protected call longjmps — and out-of-memory, the one
// failure that actually fires here, does exactly that. Stack slots need no
// cleanup: an unwinding error resets `L->top` past them for free. The cost is
// a deeper stack (up to ~2*N slots live at once), so we CheckStack up front.
//
// `positional` false sets the globals but calls the handler with no
// arguments — the vanilla shape, while the switch is off.
void RunCore(void *L, int handlerRef, void *frame, int firstArgIdx, int n, bool positional) {
    using namespace Game::Lua;
    // Headroom for the saved-old block plus the transient set/call pushes.
    if (CheckStack(L, 2 * n + 16) == 0)
        return;

    // When so, `event` (a global the dispatcher has set) is prepended as the
    // first positional to mirror retail's (self, event, arg1..argN).
    const bool isOnEvent = IsOnEvent(handlerRef, frame);
    const int selfRef = Game::Lua::ObjectLuaRef(frame);
    char argName[16];

    // --- save the globals we're about to clobber, onto the stack ------------
    // Absolute indices, live until the closing SetTop(base):
    //   base+1             = old _G.this
    //   base+2 .. base+1+n = old _G.arg1..argN
    // Raw reads, as 3.3.5's FrameScript_Execute saves them: a `_G` __index
    // (a strict-globals guard) never runs for an unset argN.
    const int base = GetTop(L);
    PushString(L, "this");
    RawGet(L, GLOBALS_INDEX); // [.. oldThis]
    for (int k = 0; k < n; ++k) {
        ArgName(argName, k + 1);
        PushString(L, argName);
        RawGet(L, GLOBALS_INDEX); // [.. oldArgk]
    }

    // --- set the new globals (each op is stack-neutral) ---------------------
    PushString(L, "this");
    RawGetI(L, REGISTRY_INDEX, selfRef); // frame object
    SetTable(L, GLOBALS_INDEX);
    for (int k = 0; k < n; ++k) {
        ArgName(argName, k + 1);
        PushString(L, argName);
        PushValue(L, firstArgIdx + k);
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
    int nargs = 0;
    if (positional) {
        RawGetI(L, REGISTRY_INDEX, selfRef); // self
        if (isOnEvent) { // event goes before the args, retail-style
            PushString(L, "event");
            RawGet(L, GLOBALS_INDEX);
        }
        for (int k = 0; k < n; ++k)
            PushValue(L, firstArgIdx + k);
        nargs = 1 + (isOnEvent ? 1 : 0) + n;
    }
    PCall(L, nargs, 0, errIdx);
    SetTop(L, callBase); // drop the message handler + any leftover error object

    // --- restore globals from the saved block, then drop it -----------------
    for (int k = 0; k < n; ++k) {
        ArgName(argName, k + 1);
        PushString(L, argName);
        PushValue(L, base + 2 + k);
        SetTable(L, GLOBALS_INDEX);
    }
    PushString(L, "this");
    PushValue(L, base + 1);
    SetTable(L, GLOBALS_INDEX);
    SetTop(L, base);
}

// Asks the providers for this run's trailing boolean; false when none takes
// one.
bool FindTrailing(int handlerRef, void *frame, bool *value) {
    for (AutoTrailingBool *t = g_trailing; t != nullptr; t = t->next)
        if (t->fn(handlerRef, frame, value))
            return true;
    return false;
}

// Lua 5.0 internal offsets for reading a handler's arity — verified against the
// engine's own luaD_precall (0x006F6050) / lua_dump (0x006F4370) /
// lua_iscfunction (0x006F34A0):
//   lua_State: top @ +0x08 (TValue *, next free slot)
//   TValue:    tag @ +0x00 (int); GC/closure pointer @ +0x08  (16-byte TValue)
//   Closure:   isC byte @ +0x06; union member (C func / Proto) @ +0x0C
//   CClosure:  C function @ +0x0C (same union slot); upvalue[i] (TValue) @
//              +0x10 + i*0x10 — lua_pushcclosure (0x006F3920) copies them there
// Single-use here, so kept local (like the lua_State field offsets elsewhere).
// The Proto fields this reads (numparams @ +0x45, is_vararg @ +0x46) are shared
// with LuaSyntax::Upvalues and live in Offsets.h as OFF_LUA_PROTO_*.
constexpr uintptr_t OFF_LUASTATE_TOP = 0x08;
constexpr uintptr_t OFF_TVALUE_GC = 0x08;
constexpr uintptr_t OFF_CLOSURE_ISC = 0x06;
constexpr uintptr_t OFF_LCLOSURE_PROTO = 0x0C;
constexpr uintptr_t OFF_CCLOSURE_FN = 0x0C;
constexpr uintptr_t OFF_CCLOSURE_UPVALUES = 0x10;
constexpr int kLuaTFunction = 6;
constexpr uintptr_t kTValueSize = 0x10;

// A HookScript chain nests one wrapper per hook. Past this depth, answer
// "yes": positional args are harmless to a handler that ignores them, so
// erring that way costs only the slower path.
constexpr int kMaxWrapperDepth = 32;

// Does the function in TValue `tv` observe positional args? A Lua closure
// does when it declares a parameter or is vararg — a modern
// `function(self, delta)` / `function(self, ...)`. The hook wrapper
// (`HookSecureFunc::Wrapper`, what HookScript installs) forwards its args to
// both functions it holds, so it does when either of them does; so does the
// dispatcher of a script with intrinsic bindings (Frame::ScriptBindings) for
// its three. Any other C
// closure may read them: a frame method bound directly
// (`SetScript("OnDragStart", f.StartMoving)`) takes `self` from argument 1
// and raises "Attempt to find 'this'" without it — 1.12's methods never fall
// back to the `this` global — so it gets them, as every handler does in 3.3.5.
bool FunctionWantsArgs(const void *tv, int depth) {
    if (Game::Read<int>(tv, 0) != kLuaTFunction)
        return false;
    const void *cl =
        reinterpret_cast<const void *>(Game::Read<uintptr_t>(tv, OFF_TVALUE_GC));
    if (cl == nullptr)
        return false;
    if (Game::Read<uint8_t>(cl, OFF_CLOSURE_ISC) == 0) { // Lua closure
        const void *proto = reinterpret_cast<const void *>(
            Game::Read<uintptr_t>(cl, OFF_LCLOSURE_PROTO));
        return proto != nullptr &&
               (Game::Read<uint8_t>(proto, Offsets::OFF_LUA_PROTO_NUMPARAMS) > 0 ||
                Game::Read<uint8_t>(proto, Offsets::OFF_LUA_PROTO_IS_VARARG) != 0);
    }
    const uintptr_t fn = Game::Read<uintptr_t>(cl, OFF_CCLOSURE_FN);
    int forwarded = 0;
    if (fn == reinterpret_cast<uintptr_t>(&HookSecureFunc::Wrapper))
        forwarded = 2; // original, hook
    else if (fn == reinterpret_cast<uintptr_t>(&Frame::ScriptBindings::Dispatcher))
        forwarded = Frame::ScriptBindings::BINDING_COUNT; // precall, extrinsic, postcall
    else
        return true;
    if (depth >= kMaxWrapperDepth)
        return true;
    const auto *upvalues = static_cast<const uint8_t *>(cl) + OFF_CCLOSURE_UPVALUES;
    for (int i = 0; i < forwarded; ++i)
        if (FunctionWantsArgs(upvalues + i * kTValueSize, depth + 1))
            return true;
    return false;
}

// --- The runners' lua_pcall, redirected -------------------------------------
// Each runner calls `lua_pcall(L, nargs, nresults, errfunc)` with [errfunc,
// handler] on top, `this` and arg1..arg`count` already set. A handler that
// observes positional args (FunctionWantsArgs, read straight off the top) gets
// `(self, [event,] arg1..argN [, trailing])`, each argN read back raw from the
// global the runner just set; any other handler, and every run while the
// switch is off, gets the engine's call unchanged.
//
// A trailing boolean (AutoTrailingBool) also becomes global arg`count+1` for
// the call, as 3.3.5 sets it. Its old value waits on the stack below the
// errfunc, where the runner's own pops after the call never reach it.
int __fastcall RunnerPCall(void *L, int nargs, int nresults, int errfunc, int handlerRef,
                           void *frame, int count) {
    using namespace Game::Lua;
    const void *handler = reinterpret_cast<const void *>(
        Game::Read<uintptr_t>(L, OFF_LUASTATE_TOP) - kTValueSize);
    // Headroom for self, event, the args, the trailing value and its save.
    if (!g_enabled || !FunctionWantsArgs(handler, 0) || CheckStack(L, count + 8) == 0)
        return PCall(L, nargs, nresults, errfunc);

    bool trailing = false;
    const bool hasTrailing = FindTrailing(handlerRef, frame, &trailing);
    char trailingName[16];
    int saved = 0;
    if (hasTrailing) {
        ArgName(trailingName, count + 1);
        PushString(L, trailingName);
        RawGet(L, GLOBALS_INDEX); // [.. errfunc handler old]
        Insert(L, -3);            // [.. old errfunc handler]
        saved = GetTop(L) - 2;
        PushString(L, trailingName);
        PushBool(L, trailing);
        SetTable(L, GLOBALS_INDEX);
    }

    int pushed = 1;
    if (frame != nullptr)
        RawGetI(L, REGISTRY_INDEX, Game::Lua::ObjectLuaRef(frame)); // self
    else
        PushNil(L);
    if (IsOnEvent(handlerRef, frame)) { // event goes before the args, retail-style
        PushString(L, "event");
        RawGet(L, GLOBALS_INDEX);
        ++pushed;
    }
    char argName[16];
    for (int k = 1; k <= count; ++k) {
        ArgName(argName, k);
        PushString(L, argName);
        RawGet(L, GLOBALS_INDEX);
    }
    pushed += count;
    if (hasTrailing) {
        PushBool(L, trailing);
        ++pushed;
    }
    // Both runners pass errfunc = -2, relative to the top: it moves down by
    // every argument pushed above it.
    const int status = PCall(L, nargs + pushed, nresults, errfunc - pushed);

    if (hasTrailing) {
        PushString(L, trailingName);
        PushValue(L, saved);
        SetTable(L, GLOBALS_INDEX);
        Remove(L, saved);
    }
    return status;
}

// The runners reach RunnerPCall through these. ECX/EDX (L, nargs) and the
// pcall's two stack arguments pass through; each thunk adds the run's handler
// ref, frame and argument count from the runner's own frame (see
// CALL_FRAME_RUN_SCRIPT_ARGS_PCALL), then returns as lua_pcall does (`ret 8`).
//
// FUN_FRAME_RUN_SCRIPT_ARGS: [ebp+8] handlerRef, [ebp+0Ch] frame, [ebp-4] the
// number of argN its switch set — every spec it ran, one added at runtime
// included.
__declspec(naked) void RunArgsPCallThunk() {
    __asm {
        push dword ptr [ebp - 4]   // count
        push dword ptr [ebp + 0Ch] // frame
        push dword ptr [ebp + 8]   // handlerRef
        push dword ptr [esp + 14h] // errfunc
        push dword ptr [esp + 14h] // nresults
        call RunnerPCall
        ret 8
    }
}

// FUN_FRAME_INVOKE_SCRIPT: [ebp-8] handlerRef, EDI frame; it sets no argN.
__declspec(naked) void InvokePCallThunk() {
    __asm {
        push 0                     // count
        push edi                   // frame
        push dword ptr [ebp - 8]   // handlerRef
        push dword ptr [esp + 14h] // errfunc
        push dword ptr [esp + 14h] // nresults
        call RunnerPCall
        ret 8
    }
}

// Runs on both registration passes, so the glue screen's scripts get the
// positional args too. Idempotent.
void PatchCode() {
    Common::RedirectCall(Offsets::CALL_FRAME_RUN_SCRIPT_ARGS_PCALL, Offsets::LUA_PCALL,
                         reinterpret_cast<const void *>(&RunArgsPCallThunk));
    Common::RedirectCall(Offsets::CALL_FRAME_INVOKE_SCRIPT_PCALL, Offsets::LUA_PCALL,
                         reinterpret_cast<const void *>(&InvokePCallThunk));
}

const Game::GlueModuleAutoRegister _gluePatch{&PatchCode};

// --- Runner co-hooks: observers and the per-handler `event` ------------------
using RunArgs_t = void(__cdecl *)(int handlerRef, void *frame, const char *fmt,
                                  const void *vaPtr);
RunArgs_t g_origRunArgs = nullptr;

void __cdecl RunArgs_h(int handlerRef, void *frame, const char *fmt,
                       const void *vaPtr) {
    void *L = Game::Lua::State();
    Notify(true, handlerRef, frame);
    const int savedEvent = SaveEvent(L, handlerRef, frame);
    g_origRunArgs(handlerRef, frame, fmt, vaPtr);
    RestoreEvent(L, savedEvent);
    Notify(false, handlerRef, frame);
}

// Same address Tooltip::SetEvents calls directly to fire OnTooltipSet*; with
// the switch enabled those fire with (self) too, which is the modern shape.
using Invoke_t = void(__fastcall *)(int handlerRef, void *frame);
Invoke_t g_origInvoke = nullptr;

void __fastcall Invoke_h(int handlerRef, void *frame) {
    void *L = Game::Lua::State();
    Notify(true, handlerRef, frame);
    const int savedEvent = SaveEvent(L, handlerRef, frame);
    g_origInvoke(handlerRef, frame);
    RestoreEvent(L, savedEvent);
    Notify(false, handlerRef, frame);
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
    PatchCode();
    Game::Lua::RegisterGlobalFunction("SetModernScriptArgs",
                                      &Script_SetModernScriptArgs);
    Game::Lua::RegisterGlobalFunction("GetModernScriptArgs",
                                      &Script_GetModernScriptArgs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void RunWithValues(void *L, int handlerRef, void *frame, int firstArgIdx, int nargs) {
    if (handlerRef == 0 || frame == nullptr)
        return;
    Notify(true, handlerRef, frame);
    const int savedEvent = SaveEvent(L, handlerRef, frame);
    RunCore(L, handlerRef, frame, firstArgIdx, nargs, /*positional*/ g_enabled);
    RestoreEvent(L, savedEvent);
    Notify(false, handlerRef, frame);
}

AutoObserve::AutoObserve(Observer f) : fn(f), next(g_observers) { g_observers = this; }

AutoTrailingBool::AutoTrailingBool(TrailingBool f) : fn(f), next(g_trailing) { g_trailing = this; }

void RunSlotWithValues(void *L, const uint32_t *slot, void *frame, int firstArgIdx, int nargs) {
    if (slot == nullptr || slot[0] == 0)
        return;
    // 0x00702690: depth up, context from the slot, run, restore the context
    // while the depth is still nonzero, depth down and floored at 0.
    auto &context = Game::Ref<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    auto &depth = Game::Ref<int32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH);
    const uint32_t saved = context;
    ++depth;
    context = slot[1];
    RunWithValues(L, static_cast<int>(slot[0]), frame, firstArgIdx, nargs);
    if (depth != 0)
        context = saved;
    if (--depth <= 0)
        depth = 0;
}

} // namespace Frame::ScriptArgs
