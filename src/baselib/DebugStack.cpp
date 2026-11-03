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

// `debugstack([thread,] [start [, count1 [, count2]]])` — the stack of a
// coroutine, as 4.3.4's Script_debugstack takes it: with a thread first,
// the levels are that thread's and `start` defaults to 0 (the thread's
// innermost level) instead of 1.
//
// 1.12's debugstack (FUN_SCRIPT_DEBUGSTACK) has no thread argument, and a
// thread falls through its argument checks so that it returns the thread
// itself: `msg .. debugstack(co)` raises "attempt to concatenate a thread
// value" in the coroutine error paths of WeakAuras, Questie's ThreadLib,
// MRT and Zygor.
//
// The engine's function still does the work, so its text and its head and
// tail windows stay exactly as they are. It reads levels through six calls
// (five lua_getstack, one lua_getinfo), and those calls are redirected to
// read from the state Script_debugstack names for the call: the thread, or
// the calling state when there is none. Script_debugstack replaces the
// global and is the engine function's only way in, so the state is set for
// every call; an error part way cannot leave a thread behind for the next.
//
// Registered on both Lua states, like the base-library helpers.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <initializer_list>

namespace BaseLib::DebugStack {

namespace {

namespace Lua = Game::Lua;

// The state whose levels the engine's debugstack reads, or null for its own.
void *g_levelsOf = nullptr;

int __fastcall GetStack(void *L, int level, void *ar) {
    return Lua::GetStack(g_levelsOf ? g_levelsOf : L, level, ar);
}

int __fastcall GetInfo(void *L, const char *what, void *ar) {
    return Lua::GetInfo(g_levelsOf ? g_levelsOf : L, what, ar);
}

int __fastcall Script_debugstack(void *L) {
    const auto engineDebugStack =
        reinterpret_cast<Lua::CFunction>(Offsets::FUN_SCRIPT_DEBUGSTACK);
    g_levelsOf = nullptr;
    if (Lua::Type(L, 1) == Lua::TYPE_THREAD) {
        g_levelsOf = Lua::ToThread(L, 1);
        Lua::Remove(L, 1);
        if (!Lua::IsNumber(L, 1)) { // start defaults to 0 for a thread
            Lua::PushNumber(L, 0);
            Lua::Insert(L, 1);
        }
    }
    const int results = engineDebugStack(L);
    g_levelsOf = nullptr;
    return results;
}

void Apply() {
    const auto getStack = reinterpret_cast<const void *>(&GetStack);
    for (const uintptr_t site :
         {Offsets::CALL_DEBUGSTACK_GETSTACK_FIRST, Offsets::CALL_DEBUGSTACK_GETSTACK_TAIL_PROBE,
          Offsets::CALL_DEBUGSTACK_GETSTACK_TAIL_START, Offsets::CALL_DEBUGSTACK_GETSTACK_TAIL_SKIP,
          Offsets::CALL_DEBUGSTACK_GETSTACK_NEXT})
        Common::RedirectCall(site, Offsets::LUA_GET_STACK, getStack);
    Common::RedirectCall(Offsets::CALL_DEBUGSTACK_GETINFO, Offsets::LUA_GET_INFO,
                         reinterpret_cast<const void *>(&GetInfo));
}

void RegisterInGame() {
    Apply();
    Lua::RegisterGlobalFunction("debugstack", &Script_debugstack);
}

void RegisterGlue() {
    Apply();
    Lua::RegisterGlueFunction("debugstack", &Script_debugstack);
}

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::DebugStack
