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

// `collectgarbage(opt [, arg])` — accept Lua 5.1's string options on the
// 5.0 collector. 5.0's collectgarbage takes only an optional NUMBER (a KB
// threshold for the next cycle) and errors on every 5.1 option (verified
// in-game: `collectgarbage("count")` → "bad argument #1 (number expected,
// got string)"). Shim dispatch:
//
//   "count"    → KB of Lua memory in use — the engine's own gcinfo()
//                value, the same number 5.1's "count" reports.
//   "collect"  → original collectgarbage(0): threshold 0 forces a full
//                cycle, the canonical 5.0 "collect now". Returns 0.
//   "step"     → no-op returning true ("cycle finished"): the 5.0 GC
//                cannot step incrementally, and true is the loop-safe
//                sentinel (`repeat until collectgarbage("step")` exits
//                immediately instead of spinning forever).
//   "stop" / "restart" / "setpause" / "setstepmul"
//              → accepted no-ops returning 0 — the 5.0 GC has no toggle
//                or tuning; callers just lose the optimization.
//   number / no argument
//              → forwarded to the original untouched (5.0 threshold
//                semantics preserved for vanilla-era callers).
//
// The originals (`collectgarbage`, `gcinfo`) are function objects stashed
// in the state's registry at registration, BEFORE the global is
// overwritten — no raw engine address needed, and each /reload re-stashes
// the freshly re-registered engine original (module callbacks run after
// the engine's own registration pass). Registered on BOTH states.

#include "Game.h"

#include <cstring>

namespace BaseLib::CollectGarbage {

namespace {

constexpr const char *kOrigCollect = "__classicapi_orig_collectgarbage";
constexpr const char *kOrigGcInfo = "__classicapi_orig_gcinfo";

// Pushes registry[key]; true when it holds a function (left on the
// stack), false otherwise (stack unchanged).
bool PushStashed(void *L, const char *key) {
    Game::Lua::PushString(L, key);
    Game::Lua::RawGet(L, Game::Lua::REGISTRY_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION)
        return true;
    Game::Lua::SetTop(L, -2);
    return false;
}

int __fastcall Script_collectgarbage(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_STRING) {
        const char *opt = Game::Lua::ToString(L, 1);
        if (std::strcmp(opt, "count") == 0) {
            if (PushStashed(L, kOrigGcInfo)) {
                Game::Lua::Call(L, 0, 1); // gcinfo's first return = KB in use
                return 1;
            }
            Game::Lua::PushNumber(L, 0.0); // no gcinfo on this state
            return 1;
        }
        if (std::strcmp(opt, "collect") == 0) {
            if (PushStashed(L, kOrigCollect)) {
                Game::Lua::PushNumber(L, 0.0); // threshold 0 = collect now
                Game::Lua::Call(L, 1, 0);
            }
            Game::Lua::PushNumber(L, 0.0); // 5.1 returns 0 for "collect"
            return 1;
        }
        if (std::strcmp(opt, "step") == 0) {
            Game::Lua::PushBool(L, true); // "cycle finished" — see header
            return 1;
        }
        if (std::strcmp(opt, "stop") == 0 || std::strcmp(opt, "restart") == 0 ||
            std::strcmp(opt, "setpause") == 0 ||
            std::strcmp(opt, "setstepmul") == 0) {
            Game::Lua::PushNumber(L, 0.0);
            return 1;
        }
        Game::Lua::Error(L, "bad argument #1 to 'collectgarbage' (invalid option '%s')",
                         opt);
        return 0; // unreachable
    }

    // Number or no argument: original 5.0 threshold behavior, forwarded.
    const int n = Game::Lua::GetTop(L);
    if (!PushStashed(L, kOrigCollect))
        return 0; // no original captured — inert
    for (int idx = 1; idx <= n; ++idx)
        Game::Lua::PushValue(L, idx);
    Game::Lua::Call(L, n, Game::Lua::MULTRET);
    return Game::Lua::GetTop(L) - n;
}

// Stashes the function at `_G[name]` under registry[key]; true when a
// function was actually stashed.
bool Stash(void *L, const char *name, const char *key) {
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX); // [fn?]
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, -2);
        return false;
    }
    Game::Lua::PushString(L, key); // [fn, key]
    Game::Lua::Insert(L, -2);      // [key, fn]
    Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX);
    return true;
}

const Game::Doc::Field kCollectgarbageArgs[] = {
    Game::Doc::Opt("opt", "string", nullptr,
                   "\"collect\", \"count\", \"step\", \"stop\", \"restart\", \"setpause\" or "
                   "\"setstepmul\"; a number or nothing sets the KB threshold for the next "
                   "collection, 0 collects now."),
    Game::Doc::Opt("arg", "number", nullptr, "Has no effect."),
};
const Game::Doc::Field kCollectgarbageRets[] = {
    Game::Doc::Opt("result", "any", nullptr,
                   "The KB of Lua memory in use for \"count\", true for \"step\", 0 for the other "
                   "options, nothing for a threshold."),
};
const Game::Doc::Function kCollectgarbage{
    "Runs a garbage collection or returns the memory in use; the other options are accepted "
    "and do nothing.",
    kCollectgarbageArgs, kCollectgarbageRets, "LuaGlobals"};

void DoRegister(bool glue) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    // Stash BEFORE overwriting; if the engine original is somehow absent,
    // leave the global untouched rather than installing a dead shim.
    if (!Stash(L, "collectgarbage", kOrigCollect))
        return;
    Stash(L, "gcinfo", kOrigGcInfo); // optional; "count" degrades to 0
    if (glue)
        Game::Lua::RegisterGlueFunction("collectgarbage", &Script_collectgarbage,
                                        &kCollectgarbage);
    else
        Game::Lua::RegisterGlobalFunction("collectgarbage", &Script_collectgarbage,
                                          &kCollectgarbage);
}

void RegisterInGame() { DoRegister(false); }
void RegisterGlue() { DoRegister(true); }

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace BaseLib::CollectGarbage
