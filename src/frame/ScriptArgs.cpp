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
// The reimplemented path is taken ONLY for a handler that can observe
// positional args: a Lua function that declares a parameter or is vararg
// (checked via its Proto), or a C function such as a frame method bound
// directly — see FunctionWantsArgs. A Lua handler that declares NO parameters
// can't observe positional args, so pushing
// them is a no-op — those route through the original engine runner unchanged:
// provably identical to vanilla, and OFF the engine's hottest Lua path (the vast
// majority of vanilla handlers, which read `this`/`arg1`, are param-less). So a
// param-less vanilla handler is untouched; only a param-declaring handler sees
// the `(self, [event,] arg1..argN)` shape. The one exception is a fire with
// more values than the engine runner's 19: 3.3.5 sets every argN, so that
// fire takes the reimplemented path whatever the handler.
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
// state. While disabled both detours are a straight passthrough to the original
// runner (exact vanilla behavior, zero reimplementation risk) — the OFF escape
// valve, since this lands on the hottest Lua path in the engine
// (FUN_FRAME_RUN_SCRIPT_ARGS fires for every OnUpdate frame).

#include "frame/ScriptArgs.h"

#include "Game.h"
#include "HookSecureFunc.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdio>

namespace Frame::ScriptArgs {

namespace {

// Runtime switch. Default ON (core 5.1 behavior). When false, both detours
// tail-call the original runner unchanged (exact vanilla behavior, no
// reimplementation risk) — the OFF escape valve for this hot path.
bool g_enabled = true;

// Chained at static-init time by each `AutoObserve` constructor.
AutoObserve *g_observers = nullptr;

void Notify(bool begin, int handlerRef, void *frame) {
    for (AutoObserve *o = g_observers; o != nullptr; o = o->next)
        o->fn(begin, handlerRef, frame);
}

// Chained at static-init time by each `AutoTrailingBool` constructor.
AutoTrailingBool *g_trailing = nullptr;

// The engine runner sets only arg1..arg19 (0x704F10 stops at the 19th spec,
// `cmp [ebp-4],0x13; jge` at 0x704F73). 3.3.5's FrameScript_Execute has no
// limit, so a fire with more specs than this takes the reimplemented path.
constexpr int kEngineArgLimit = 19;

using Game::Lua::RawGetI;

// Writes "arg" + k into buf.
void ArgName(char (&buf)[16], int k) { std::snprintf(buf, sizeof buf, "arg%d", k); }

// Counts the values in the runner's printf-style format: each %d/%u/%f/%s.
// Any other letter after `%` is skipped and consumes no vararg, as in the
// engine runner's switch.
int CountArgs(const char *fmt) {
    if (fmt == nullptr)
        return 0;
    int n = 0;
    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p != '%')
            continue;
        ++p;
        if (*p == '\0')
            break;
        if (*p == 'd' || *p == 'u' || *p == 'f' || *p == 's')
            ++n;
    }
    return n;
}

// Where RunCore's arguments come from: the runner's printf-style format over
// its packed vararg buffer (%d/%u/%s 4 bytes, %f 8), or `n` Lua values already
// on the stack from absolute index `stackBase` (they sit below everything
// RunCore pushes, so the index holds). `hasTrailing` appends one boolean after
// them (see AutoTrailingBool).
struct ArgSource {
    const char *fmt = nullptr;
    const void *vaPtr = nullptr;
    int stackBase = 0;
    int n = 0;
    bool hasTrailing = false;
    bool trailing = false;
    int Count() const { return n + (hasTrailing ? 1 : 0); }
};

// Pushes an ArgSource's values in order, one per PushNext; a new cursor
// starts again from the first.
class ArgCursor {
public:
    explicit ArgCursor(const ArgSource &src)
        : src_(src), p_(src.fmt), cur_(static_cast<const char *>(src.vaPtr)) {}

    void PushNext(void *L) {
        using namespace Game::Lua;
        if (k_ >= src_.n) {
            PushBool(L, src_.trailing);
            ++k_;
            return;
        }
        if (p_ == nullptr) {
            PushValue(L, src_.stackBase + k_++);
            return;
        }
        for (; *p_ != '\0'; ++p_) {
            if (*p_ != '%')
                continue;
            ++p_;
            const char spec = *p_;
            if (spec == '\0')
                return;
            switch (spec) {
            case 'd':
                PushNumber(L, static_cast<double>(*reinterpret_cast<const int *>(cur_)));
                cur_ += 4;
                break;
            case 'u':
                PushNumber(L, static_cast<double>(
                                  *reinterpret_cast<const unsigned int *>(cur_)));
                cur_ += 4;
                break;
            case 'f':
                PushNumber(L, *reinterpret_cast<const double *>(cur_));
                cur_ += 8;
                break;
            case 's': // a null string pushes nil, as lua_pushstring does
                PushString(L, *reinterpret_cast<const char *const *>(cur_));
                cur_ += 4;
                break;
            default:
                continue; // unknown spec — consume nothing
            }
            ++p_;
            ++k_;
            return;
        }
    }

private:
    const ArgSource &src_;
    const char *p_;
    const char *cur_;
    int k_ = 0;
};

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
//
// `positional` false sets the globals but calls the handler with no
// arguments — the vanilla shape, for RunWithValues while the switch is off.
bool RunCore(void *L, int handlerRef, void *frame, const ArgSource &src,
             bool positional) {
    using namespace Game::Lua;
    // Headroom for the saved-old block plus the transient set/call pushes.
    if (CheckStack(L, 2 * src.Count() + 16) == 0)
        return false;

    const bool haveThis = frame != nullptr;
    // When so, `event` (a global the dispatcher has set) is prepended as the
    // first positional to mirror retail's (self, event, arg1..argN).
    const bool isOnEvent = IsOnEvent(handlerRef, frame);
    const int selfRef = haveThis ? Game::Lua::ObjectLuaRef(frame) : 0;

    const int n = src.Count();
    char argName[16];

    // --- save the globals we're about to clobber, onto the stack ------------
    // Absolute indices, live until the closing SetTop(base):
    //   base+1                = old _G.this        (if haveThis)
    //   savedArgBase+1 .. +n   = old _G.arg1..argN
    // Raw reads, as 3.3.5's FrameScript_Execute saves them: a `_G` __index
    // (a strict-globals guard) never runs for an unset argN.
    const int base = GetTop(L);
    if (haveThis) {
        PushString(L, "this");
        RawGet(L, GLOBALS_INDEX); // [.. oldThis]
    }
    const int savedArgBase = base + (haveThis ? 1 : 0);
    for (int k = 0; k < n; ++k) {
        ArgName(argName, k + 1);
        PushString(L, argName);
        RawGet(L, GLOBALS_INDEX); // [.. oldArgk]
    }

    // --- set the new globals (each op is stack-neutral) ---------------------
    if (haveThis) {
        PushString(L, "this");
        RawGetI(L, REGISTRY_INDEX, selfRef); // frame object
        SetTable(L, GLOBALS_INDEX);
    }
    ArgCursor globals(src);
    for (int k = 0; k < n; ++k) {
        ArgName(argName, k + 1);
        PushString(L, argName);
        globals.PushNext(L);
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
        if (haveThis)
            RawGetI(L, REGISTRY_INDEX, selfRef); // self
        else
            PushNil(L);
        if (isOnEvent) { // event goes before the args, retail-style
            PushString(L, "event");
            RawGet(L, GLOBALS_INDEX);
        }
        ArgCursor args(src);
        for (int k = 0; k < n; ++k)
            args.PushNext(L);
        nargs = 1 + (isOnEvent ? 1 : 0) + n;
    }
    PCall(L, nargs, 0, errIdx);
    SetTop(L, callBase); // drop the message handler + any leftover error object

    // --- restore globals from the saved block, then drop it -----------------
    for (int k = 0; k < n; ++k) {
        ArgName(argName, k + 1);
        PushString(L, argName);
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

// Asks the providers for this run's trailing boolean.
void FindTrailing(int handlerRef, void *frame, ArgSource &src) {
    for (AutoTrailingBool *t = g_trailing; t != nullptr; t = t->next) {
        bool value = false;
        if (t->fn(handlerRef, frame, &value)) {
            src.hasTrailing = true;
            src.trailing = value;
            return;
        }
    }
}

// The runner path: arguments from the engine's printf-style format.
bool RunModern(int handlerRef, void *frame, const char *fmt, const void *vaPtr) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return false;
    ArgSource src;
    src.fmt = fmt;
    src.vaPtr = vaPtr;
    src.n = CountArgs(fmt);
    FindTrailing(handlerRef, frame, src);
    return RunCore(L, handlerRef, frame, src, /*positional*/ true);
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
// both functions it holds, so it does when either of them does. Any other C
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
    if (Game::Read<uintptr_t>(cl, OFF_CCLOSURE_FN) !=
        reinterpret_cast<uintptr_t>(&HookSecureFunc::Wrapper))
        return true;
    if (depth >= kMaxWrapperDepth)
        return true;
    const auto *upvalues = static_cast<const uint8_t *>(cl) + OFF_CCLOSURE_UPVALUES;
    return FunctionWantsArgs(upvalues, depth + 1) ||              // original
           FunctionWantsArgs(upvalues + kTValueSize, depth + 1);  // hook
}

// Does this handler actually observe positional args? A vanilla handler reads
// this/arg1 globals and declares NO parameters, so pushing positional args to
// it is observably identical to vanilla — those route through the original
// engine runner instead (exact vanilla, and off the hottest Lua path). Only a
// handler FunctionWantsArgs accepts takes the reimplemented positional path.
//
// Reads the closures directly (the engine exposes no arity via the C API);
// every read is bounded — a non-function ref or a null Proto falls through to
// "no".
bool HandlerWantsArgs(void *L, int handlerRef) {
    RawGetI(L, Game::Lua::REGISTRY_INDEX, handlerRef); // push registry[handlerRef]
    const void *tv = reinterpret_cast<const void *>(
        Game::Read<uintptr_t>(L, OFF_LUASTATE_TOP) - kTValueSize);
    const bool wants = FunctionWantsArgs(tv, 0);
    Game::Lua::SetTop(L, -2); // pop the handler
    return wants;
}

// --- FUN_FRAME_RUN_SCRIPT_ARGS co-hook (scripts with values) ----------------
using RunArgs_t = void(__cdecl *)(int handlerRef, void *frame, const char *fmt,
                                  const void *vaPtr);
RunArgs_t g_origRunArgs = nullptr;

void RunArgs(void *L, int handlerRef, void *frame, const char *fmt,
             const void *vaPtr) {
    // Past the engine's limit the original would drop arg20 onward, so even a
    // handler that declares no parameters takes the uncapped path.
    if (g_enabled && L != nullptr && handlerRef != 0 && fmt != nullptr &&
        (HandlerWantsArgs(L, handlerRef) || CountArgs(fmt) > kEngineArgLimit) &&
        RunModern(handlerRef, frame, fmt, vaPtr))
        return;
    g_origRunArgs(handlerRef, frame, fmt, vaPtr);
}

void __cdecl RunArgs_h(int handlerRef, void *frame, const char *fmt,
                       const void *vaPtr) {
    void *L = Game::Lua::State();
    Notify(true, handlerRef, frame);
    const int savedEvent = SaveEvent(L, handlerRef, frame);
    RunArgs(L, handlerRef, frame, fmt, vaPtr);
    RestoreEvent(L, savedEvent);
    Notify(false, handlerRef, frame);
}

// --- FUN_FRAME_INVOKE_SCRIPT co-hook (no-arg scripts) -----------------------
// Same address Tooltip::SetEvents calls directly to fire OnTooltipSet*; with
// the switch enabled those fire with (self) too, which is the modern shape.
using Invoke_t = void(__fastcall *)(int handlerRef, void *frame);
Invoke_t g_origInvoke = nullptr;

void Invoke(void *L, int handlerRef, void *frame) {
    if (g_enabled && L != nullptr && handlerRef != 0 && frame != nullptr &&
        HandlerWantsArgs(L, handlerRef) &&
        RunModern(handlerRef, frame, /*fmt*/ nullptr, /*vaPtr*/ nullptr))
        return;
    g_origInvoke(handlerRef, frame);
}

void __fastcall Invoke_h(int handlerRef, void *frame) {
    void *L = Game::Lua::State();
    Notify(true, handlerRef, frame);
    const int savedEvent = SaveEvent(L, handlerRef, frame);
    Invoke(L, handlerRef, frame);
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
    ArgSource src;
    src.stackBase = firstArgIdx;
    src.n = nargs;
    Notify(true, handlerRef, frame);
    const int savedEvent = SaveEvent(L, handlerRef, frame);
    RunCore(L, handlerRef, frame, src, /*positional*/ g_enabled);
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
