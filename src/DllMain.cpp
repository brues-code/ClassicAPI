// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#include "Common.h"
#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "nameplate/Walk.h"
#include "player/NameCache.h"

static Game::FrameScript_Initialize_t FrameScript_Initialize_o = nullptr;
static Game::LoadScriptFunctions_t LoadScriptFunctions_o = nullptr;
static Game::LoadGlueScriptFunctions_t LoadGlueScriptFunctions_o = nullptr;

// `Frame::RegisterEvent` is `__thiscall(this, eventName)`. MSVC can't emit
// __thiscall on free functions, but a __fastcall function with a dummy EDX
// arg matches the same register layout (ECX=this, EDX unused, stack=name).
using FrameRegisterEvent_t = void(__fastcall *)(void *frame, void *edx,
                                                 const char *eventName);
static FrameRegisterEvent_t FrameRegisterEvent_o = nullptr;

static void __fastcall InvalidFunctionPtrCheck_h() {}

static bool __fastcall FrameScript_Initialize_h() {
    // BEFORE the engine tears down the old event table (at the start of
    // FrameScript_Initialize), invalidate our cached slot indices. The
    // table is rebuilt at a fresh allocation; the old slots are stale.
    Event::Custom::PrepareForReload();

    // Clear the nameplate diff state so currently-visible plates
    // refire CREATED and UNIT_ADDED on the next tick — re-presents
    // them to the freshly reloaded UI, matching modern WoW. The
    // wrappers themselves now live in the engine's own registry
    // (via `FrameScript_Object::ScriptRegister`), which the engine
    // tears down and rebuilds across the Lua reset — no per-module
    // cache to clear here.
    NamePlate::Events::PrepareForReload();

    // Persist the name cache before the engine starts tearing down.
    // This hook fires on both `/reload` and `/logout` (the engine
    // re-initializes Lua state in both cases), giving us a clean
    // deterministic save point for the common lifecycle events.
    Player::NameCache::Flush();

    FrameScript_Initialize_o();
    return true;
}

static void __fastcall LoadScriptFunctions_h() {
    LoadScriptFunctions_o();
    Game::RunModuleRegistrations();
    // Permit `Event::Custom::TryClaim` to actually write to the event
    // table from here on. Earlier writes (during the engine's own
    // boot-time `RegisterEvent` flurry, plus SuperWoWhook/etc.) can
    // race with the engine's table init and trigger `SMemFree` on slots
    // it still considers in-flight.
    Event::Custom::EnableWrites();
}

static void __stdcall LoadGlueScriptFunctions_h() {
    LoadGlueScriptFunctions_o();
    Game::RunGlueModuleRegistrations();
}

// Every Lua-side `frame:RegisterEvent(...)` is a chance to claim a slot
// for any custom event still waiting. By this point the engine's table
// is fully populated and SuperWoWhook / other DLLs have done their
// post-rebuild writes, so the table state is settled and our backwards
// walk finds genuine NULL slots near the tail.
static void __fastcall FrameRegisterEvent_h(void *frame, void *edx,
                                            const char *eventName) {
    Event::Custom::RetryClaims();
    // Throttled-internally; no-op when the cache or scan toggle is off.
    Player::NameCache::Tick();
    FrameRegisterEvent_o(frame, edx, eventName);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        if (MH_Initialize() != MH_OK)
            return FALSE;

        auto *target = reinterpret_cast<LPVOID>(Offsets::FUN_INVALID_FUNCTION_PTR_CHECK);
        if (MH_CreateHook(target, reinterpret_cast<LPVOID>(InvalidFunctionPtrCheck_h), nullptr) != MH_OK)
            return FALSE;
        if (MH_EnableHook(target) != MH_OK)
            return FALSE;

        // Four core init hooks stay here — each runs glue logic
        // (PrepareForReload, RunModuleRegistrations,
        // RunGlueModuleRegistrations, EnableWrites, RetryClaims)
        // that's tightly coupled to DllMain state, so a declarative
        // HookAutoRegister wouldn't simplify them.
        HOOK_FUNCTION(Offsets::FUN_FRAME_SCRIPT_INITIALIZE, FrameScript_Initialize_h,
                      FrameScript_Initialize_o);
        HOOK_FUNCTION(Offsets::FUN_LOAD_SCRIPT_FUNCTIONS, LoadScriptFunctions_h,
                      LoadScriptFunctions_o);
        HOOK_FUNCTION(Offsets::FUN_LOAD_GLUE_SCRIPT_FUNCTIONS,
                      LoadGlueScriptFunctions_h, LoadGlueScriptFunctions_o);
        HOOK_FUNCTION(Offsets::FUN_FRAME_REGISTER_EVENT, FrameRegisterEvent_h,
                      FrameRegisterEvent_o);

        // All feature hooks declared via `Game::HookAutoRegister` at
        // file scope in their respective modules.
        if (!Game::RunHookRegistrations())
            return FALSE;
    } else if (reason == DLL_PROCESS_DETACH) {
        // Clean /quit path. Flush before MH_Uninitialize — the cache's
        // file I/O uses Win32 directly (no MinHook involvement), so
        // either order works in practice, but flushing first lets us
        // bail early if the hook teardown ever grows side effects.
        // Hard process termination (task manager kill) bypasses this
        // path; that's an inherent OS limitation, and the 5-minute
        // backstop flush in Remember() covers the worst case there.
        Player::NameCache::Flush();
        MH_Uninitialize();
    }
    return TRUE;
}
