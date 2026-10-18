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
//
// The `coroutine` library, as 4.3.4 ships it (stock Lua 5.1's lbaselib.c
// co_funcs, opened by luaopen_base in FrameScript_Initialize for the glue AND
// the world state). WoW 1.12.1 links the C-level machinery (`lua_resume`,
// `lua_yield`, `lua_newthread`, threads as type tag 8) but never registers
// the table, so the six functions below are 5.1's, built on the engine's own
// 5.0 primitives. Where this build's 5.0 differs from 5.1, the engine's own
// predicate is mirrored (see CoStatus / AuxResume).

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Coroutine {

// lua_State / CallInfo / global_State fields — all single-consumer, local:
//   L->top @ +0x08, L->l_G @ +0x10, L->ci @ +0x14, L->base_ci @ +0x28;
//   CallInfo->state @ +0x08, CI_YIELD = 0x10 — the bit lua_yield
//   (0x006F6860) ORs in and the resume body (0x006F67B0) tests;
//   G->mainthread @ +0x50 — f_luaopen (0x006F6D70) stores the opening L.
static constexpr uintptr_t OFF_L_TOP = 0x08;
static constexpr uintptr_t OFF_L_G = 0x10;
static constexpr uintptr_t OFF_L_CI = 0x14;
static constexpr uintptr_t OFF_L_BASE_CI = 0x28;
static constexpr uintptr_t OFF_CI_STATE = 0x08;
static constexpr uint32_t CI_YIELD = 0x10;
static constexpr uintptr_t OFF_G_MAINTHREAD = 0x50;

// This build's TValue: {tt @ +0, ctx @ +4, value @ +8}, 16 bytes.
static constexpr uintptr_t kTValueSize = 0x10;

static constexpr int CO_RUN = 0;
static constexpr int CO_SUS = 1;
static constexpr int CO_NOR = 2; // resumed another coroutine
static constexpr int CO_DEAD = 3;

static const char *const kStatusNames[] = {"running", "suspended", "normal", "dead"};
// 5.1's `"cannot resume %s coroutine"` with each status name, indexed the
// same way (never used for CO_SUS).
static const char *const kCannotResume[] = {
    "cannot resume running coroutine", nullptr,
    "cannot resume normal coroutine", "cannot resume dead coroutine"};

// 5.1's costatus, on this build's 5.0 state. 5.1 asks `lua_status(co) ==
// LUA_YIELD`; this build's lua_yield never writes L->status, it sets CI_YIELD
// on the current CallInfo, so ask exactly what the engine's resume body asks:
//   ci == base_ci, empty stack  -> dead (returned, or errored and unwound)
//   ci == base_ci, function set -> suspended (created, never resumed)
//   ci >  base_ci, CI_YIELD     -> suspended (paused in yield)
//   ci >  base_ci, no CI_YIELD  -> normal (mid-resume of another coroutine)
static bool HasFrames(void *co) {
    return Game::Read<const void *>(co, OFF_L_CI) !=
           Game::Read<const void *>(co, OFF_L_BASE_CI);
}
static bool IsYielded(void *co) {
    return HasFrames(co) &&
           (Game::Read<uint32_t>(Game::Read<const void *>(co, OFF_L_CI), OFF_CI_STATE) &
            CI_YIELD) != 0;
}
static int CoStatus(void *L, void *co) {
    if (co == L)
        return CO_RUN;
    if (!HasFrames(co))
        return Game::Lua::GetTop(co) == 0 ? CO_DEAD : CO_SUS;
    return IsYielded(co) ? CO_SUS : CO_NOR;
}

// 5.1's auxresume. Returns the result count (moved onto L) or -1 with the
// error message on top of L.
//
// This build's lua_resume returns 1 for BOTH a yield and a caught error (5.1
// separates them), so the yield is recognized by IsYielded: a yielded body
// is still on co's call stack with CI_YIELD set, an errored one has been
// unwound to base_ci (leaving the message on the stack, which CoStatus
// alone would mistake for a never-resumed coroutine).
static int AuxResume(void *L, void *co, int narg) {
    const int status = CoStatus(L, co);
    if (Game::Lua::CheckStack(co, narg) == 0)
        Game::Lua::Error(L, "too many arguments to resume");
    if (status != CO_SUS) {
        Game::Lua::PushString(L, kCannotResume[status]);
        return -1;
    }
    Game::Lua::XMove(L, co, narg);
    const int s = Game::Lua::Resume(co, narg);
    if (s == 0 || IsYielded(co)) {
        const int nres = Game::Lua::GetTop(co);
        if (Game::Lua::CheckStack(L, nres + 1) == 0)
            Game::Lua::Error(L, "too many results to resume");
        Game::Lua::XMove(co, L, nres);
        return nres;
    }
    Game::Lua::XMove(co, L, 1); // the error message
    return -1;
}

static int __fastcall Script_Coroutine_Create(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_FUNCTION || Game::Lua::IsCFunction(L, 1)) {
        Game::Lua::ArgError(L, 1, "Lua function expected");
        return 0; // unreachable
    }
    Game::Lua::NewThread(L);                    // [arg1, NL]
    void *NL = Game::Lua::ToThread(L, -1);
    Game::Lua::PushValue(L, 1);                 // [arg1, NL, fn]
    Game::Lua::XMove(L, NL, 1);                 // [arg1, NL]; NL has fn
    return 1;
}

static int __fastcall Script_Coroutine_Yield(void *L) {
    return Game::Lua::Yield(L, Game::Lua::GetTop(L));
}

static int __fastcall Script_Coroutine_Resume(void *L) {
    void *co = Game::Lua::Type(L, 1) == Game::Lua::TYPE_THREAD
                   ? Game::Lua::ToThread(L, 1)
                   : nullptr;
    if (co == nullptr) {
        Game::Lua::ArgError(L, 1, "coroutine expected");
        return 0; // unreachable
    }
    const int r = AuxResume(L, co, Game::Lua::GetTop(L) - 1);
    if (r < 0) {
        Game::Lua::PushBool(L, false);
        Game::Lua::Insert(L, -2);
        return 2; // false + error message
    }
    Game::Lua::PushBool(L, true);
    Game::Lua::Insert(L, -(r + 1));
    return r + 1; // true + resume's returns
}

static int __fastcall Script_Coroutine_Status(void *L) {
    void *co = Game::Lua::Type(L, 1) == Game::Lua::TYPE_THREAD
                   ? Game::Lua::ToThread(L, 1)
                   : nullptr;
    if (co == nullptr) {
        Game::Lua::ArgError(L, 1, "coroutine expected");
        return 0; // unreachable
    }
    Game::Lua::PushString(L, kStatusNames[CoStatus(L, co)]);
    return 1;
}

// 5.1's auxwrap: a string error gets the caller's position prefixed
// (luaL_where(L, 1)) — which is what LUA_ERROR, really luaL_error, does —
// and any other error object is re-raised untouched with the raw lua_error.
static int __fastcall WrapAux(void *L) {
    void *co = Game::Lua::ToThread(L, Game::Lua::UpvalueIndex(1));
    const int r = AuxResume(L, co, Game::Lua::GetTop(L));
    if (r < 0) {
        if (Game::Lua::IsString(L, -1))
            Game::Lua::Error(L, "%s", Game::Lua::ToString(L, -1));
        using RawError_t = int(__fastcall *)(void *L);
        reinterpret_cast<RawError_t>(Offsets::LUA_RAW_ERROR)(L);
        return 0; // unreachable
    }
    return r;
}

static int __fastcall Script_Coroutine_Wrap(void *L) {
    Script_Coroutine_Create(L);              // leaves the thread on top
    Game::Lua::PushCClosure(L, &WrapAux, 1); // captures it as upvalue 1
    return 1;
}

// 5.1's corunning: the running coroutine, or nil on the main thread. 5.0's
// C API has no lua_pushthread, so the thread is pushed the way the engine's
// own pushes write a TValue: {tt = thread, ctx = the current exec context,
// value = L}, then top += one slot. Every C function has LUA_MINSTACK free
// slots, and the running thread is trivially alive.
static int __fastcall Script_Coroutine_Running(void *L) {
    const void *G = Game::Read<const void *>(L, OFF_L_G);
    if (L == Game::Read<const void *>(G, OFF_G_MAINTHREAD)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    uint8_t *top = Game::Read<uint8_t *>(L, OFF_L_TOP);
    Game::Ref<int>(top, 0) = Game::Lua::TYPE_THREAD;
    Game::Ref<int>(top, 4) = Game::Read<int>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    Game::Ref<void *>(top, 8) = L;
    Game::Ref<uint8_t *>(L, OFF_L_TOP) = top + kTValueSize;
    return 1;
}

// Both states: 4.3.4 opens the library from FrameScript_Initialize, which
// runs for the glue state (Client.cpp) and the world (GameUI.cpp).
static void RegisterFunctions() {
    Game::Lua::RegisterTableFunction("coroutine", "create", &Script_Coroutine_Create);
    Game::Lua::RegisterTableFunction("coroutine", "resume", &Script_Coroutine_Resume);
    Game::Lua::RegisterTableFunction("coroutine", "running", &Script_Coroutine_Running);
    Game::Lua::RegisterTableFunction("coroutine", "status", &Script_Coroutine_Status);
    Game::Lua::RegisterTableFunction("coroutine", "wrap", &Script_Coroutine_Wrap);
    Game::Lua::RegisterTableFunction("coroutine", "yield", &Script_Coroutine_Yield);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterFunctions};
static const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFunctions};

} // namespace Coroutine
