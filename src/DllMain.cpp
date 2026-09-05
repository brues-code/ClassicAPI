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

#include "Common.h"
#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"
#include "config/FileSwitch.h"
#include "event/Custom.h"
#include "player/NameCache.h"
#include "text/InlineTexture.h"
#include "text/InlineTexturePool.h"
#include "texture/Mask.h"
#include "texture/Transform.h"

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
    // Fire every module's PrepareForReload BEFORE the engine tears down the old
    // event table / Lua state. Each module that keeps reload-fragile state (Lua
    // refs, frame-pointer-keyed maps, cached event-slot indices) self-registers
    // via `static const Game::ReloadAutoRegister _reload{&PrepareForReload};`,
    // so this one call clears them all — a new module can't forget to wire in
    // (see the ReloadAutoRegister doc in Game.h). This hook fires on both
    // `/reload` and `/logout` (the engine re-inits the Lua state in both).
    Game::RunReloadCleanups();

    // Persist the name cache before the engine tears down — a SAVE, not a clear,
    // so it stays an explicit call rather than a ReloadAutoRegister.
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
    // World→glue teardown destroyed every world fontstring and text node the
    // inline-icon maps reference; forget them now that the glue UI is booting.
    // (Glue→world is covered by FrameScript_Initialize_h; the node-free hook
    // handles individual deaths, but bulk teardown paths may bypass it.)
    Text::InlineTexture::PrepareForReload();
    Text::InlineTexturePool::PrepareForReload();
    Texture::Transform::PrepareForReload();
    Texture::Mask::PrepareForReload();
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

// ---------------------------------------------------------------------------
// Hook install — kept OFF the Windows loader lock.
//
// Every MH_EnableHook freezes all process threads (CreateToolhelp32Snapshot +
// SuspendThread/GetThreadContext each). Doing ~90 of those from DllMain, under
// the loader lock, is the pattern MinHook documents as unsafe: on machines
// whose security stack intercepts thread suspension it stalled the remote
// LoadLibrary thread past VanillaFixes' 10-second injection deadline, so VF
// read the still-running thread as STILL_ACTIVE (259) and showed its generic
// "compatible client" error — a false diagnosis of a slow load.
//
// So DllMain installs nothing. The install runs later, on a thread that does
// NOT hold the loader lock, via one of two triggers that both funnel through
// the latched EnsureInitialized():
//   * VanillaFixes' `Load` export, called on the game's main thread after
//     injection (the sanctioned VF extension point), or
//   * a fallback worker thread we spawn from DllMain, ONLY when VanillaFixes
//     is not the loader (no VfPatcher.dll in the process), for injectors that
//     never call `Load`.
//
// The worker MUST stand down under VanillaFixes. VF creates the process
// suspended and injects every dlls.txt DLL sequentially via remote threads;
// a worker spawned from our DllMain starts the moment our own LoadLibrary
// releases the loader lock — i.e. while VF is still injecting the NEXT
// dlls.txt DLL. Its ~90-prologue install (plus the MH_ApplyQueued
// thread-freeze) then races that DLL's DllMain patching the same engine
// functions, which made SuperWoWhook.dll fail to load whenever ClassicAPI
// preceded it in dlls.txt. `Load` has no such race: VfPatcher calls it on the
// game's main thread after the process resumes, when every DllMain in the
// injection chain has already completed, serialized in dlls.txt order.
// ---------------------------------------------------------------------------

static volatile LONG g_initClaimed = 0;    // 0 until a thread takes the installer role
static volatile LONG g_initDone = 0;       // 0 until the install has finished
static volatile LONG g_initResult = 1;     // 0 = success, 1 = failure (until proven)
static volatile LONG g_mhInitialized = 0;  // MH_Initialize succeeded (gates detach teardown)

static bool CreateAndQueue(uintptr_t offset, void *hook, void **original) {
    auto *target = reinterpret_cast<LPVOID>(offset);
    if (MH_CreateHook(target, hook, original) != MH_OK)
        return false;
    // Queue only — a single MH_ApplyQueued below applies the whole batch in
    // one thread-freeze.
    if (MH_QueueEnableHook(target) != MH_OK)
        return false;
    return true;
}

static bool InstallHooks() {
    if (MH_Initialize() != MH_OK)
        return false;
    InterlockedExchange(&g_mhInitialized, 1);

    if (!CreateAndQueue(Offsets::FUN_INVALID_FUNCTION_PTR_CHECK,
                        reinterpret_cast<void *>(InvalidFunctionPtrCheck_h), nullptr))
        return false;

    // Four core init hooks — each runs glue logic (PrepareForReload,
    // RunModuleRegistrations, RunGlueModuleRegistrations, EnableWrites,
    // RetryClaims) tightly coupled to DllMain state, so a declarative
    // HookAutoRegister wouldn't simplify them.
    if (!CreateAndQueue(Offsets::FUN_FRAME_SCRIPT_INITIALIZE,
                        reinterpret_cast<void *>(FrameScript_Initialize_h),
                        reinterpret_cast<void **>(&FrameScript_Initialize_o)))
        return false;
    if (!CreateAndQueue(Offsets::FUN_LOAD_SCRIPT_FUNCTIONS,
                        reinterpret_cast<void *>(LoadScriptFunctions_h),
                        reinterpret_cast<void **>(&LoadScriptFunctions_o)))
        return false;
    if (!CreateAndQueue(Offsets::FUN_LOAD_GLUE_SCRIPT_FUNCTIONS,
                        reinterpret_cast<void *>(LoadGlueScriptFunctions_h),
                        reinterpret_cast<void **>(&LoadGlueScriptFunctions_o)))
        return false;
    if (!CreateAndQueue(Offsets::FUN_FRAME_REGISTER_EVENT,
                        reinterpret_cast<void *>(FrameRegisterEvent_h),
                        reinterpret_cast<void **>(&FrameRegisterEvent_o)))
        return false;

    // All feature hooks declared via `Game::HookAutoRegister` at file scope
    // in their respective modules (create + queue-enable, no apply yet).
    if (!Game::RunHookRegistrations())
        return false;

    // One thread-freeze that activates every queued hook at once.
    return MH_ApplyQueued() == MH_OK;
}

// Runs InstallHooks exactly once. Returns 0 on success, 1 on failure. If a
// second caller arrives while the install is in flight, it blocks briefly so
// both callers observe the real result.
static DWORD EnsureInitialized() {
    if (InterlockedCompareExchange(&g_initClaimed, 1, 0) == 0) {
        g_initResult = InstallHooks() ? 0 : 1;
        InterlockedExchange(&g_initDone, 1);
    } else {
        while (InterlockedCompareExchange(&g_initDone, 0, 0) == 0)
            Sleep(1);
    }
    return static_cast<DWORD>(g_initResult);
}

static DWORD WINAPI InitWorker(LPVOID) {
    EnsureInitialized();
    return 0;
}

// VanillaFixes calls this on the game's MAIN thread after injection
// (InitAdditionalDLLs -> GetProcAddress(module, "Load")), outside the loader
// lock and with no timeout — the sanctioned VF extension point. Returns 0 on
// success; VF reports any non-zero result to the user. Exported undecorated
// as "Load" via src/ClassicAPI.def.
extern "C" DWORD Load() { return EnsureInitialized(); }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // The ONE thing that has to happen this early, and the only module this
        // file names directly. The engine reads its config file during boot,
        // several calls before VanillaFixes invokes `Load`, so a `-config`
        // switch cannot be applied from the normal install path — by then the
        // file has been read. This is a single 4-byte store, not a hook, so it
        // carries none of the loader-lock cost described above.
        Config::FileSwitch::Apply();

        // Install nothing here (loader lock — see the block comment above).
        //
        // Under VanillaFixes, `Load` is the ONLY install trigger. VfPatcher.dll
        // is injected before any dlls.txt DLL, so its presence identifies VF as
        // the loader; and the `Load` mechanism shipped in the same VF release
        // (v1.4) as dlls.txt support itself, so any VF that loaded us will call
        // it. Spawning the worker here would race the DllMains of the dlls.txt
        // DLLs VF injects after us (see the block comment above).
        if (GetModuleHandleW(L"VfPatcher.dll") == nullptr) {
            // Any other injector: no `Load` caller exists, so install from a
            // worker off the lock. The new thread cannot run its body until
            // DllMain returns and the loader lock releases, and we never wait
            // on it. DisableThreadLibraryCalls suppressed its THREAD_ATTACH.
            HANDLE worker = CreateThread(nullptr, 0, InitWorker, nullptr, 0, nullptr);
            if (worker != nullptr)
                CloseHandle(worker);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Clean /quit path — only tear down if we actually initialized.
        // Flush before MH_Uninitialize — the cache's file I/O uses Win32
        // directly (no MinHook involvement), so either order works in
        // practice, but flushing first lets us bail early if the hook
        // teardown ever grows side effects. Hard process termination (task
        // manager kill) bypasses this path; that's an inherent OS
        // limitation, and the 5-minute backstop flush in Remember() covers
        // the worst case there.
        if (g_mhInitialized) {
            Player::NameCache::Flush();
            MH_Uninitialize();
        }
    }
    return TRUE;
}
