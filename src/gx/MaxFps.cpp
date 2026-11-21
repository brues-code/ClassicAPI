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

// Frame-rate caps: `useMaxFPS` / `maxFPS` and `useMaxFPSBk` / `maxFPSBk`,
// ported from the 3.80.2 Classic client (WowClassicGM-China.exe 3.80.2.69496).
// Names, help lines and the Graphics category are 3.80.2's.
//
// Choosing the cap (0x14243F900). The foreground cap is maxFPS when useMaxFPS
// is set, otherwise none. The background cap is maxFPSBk when useMaxFPSBk is
// set, otherwise none. The foreground flag (0x14445A6BF) selects one of the
// two. The result is never below 8. So in the background with useMaxFPSBk
// off there is no cap, even when the foreground cap is on. That is what
// 3.80.2 does (3.3.5 and 4.3.4 used min(maxFPS, maxFPSBk) instead). 3.80.2
// sets the foreground flag from window activation. 1.12 sends
// EVENT_ID_ACTIVATE for each WM_ACTIVATE, so we listen to that event.
//
// The wait (0x1401ECA00 → 0x14032E150) works in QPC ticks. A new cap sets the
// interval to freq / cap and starts a deadline at now + interval. Each frame:
//   - if now is before the deadline, sleep until the deadline, then
//     deadline += interval, so the rate does not drift;
//   - otherwise, deadline = now + interval.
// To sleep for n ns it calls Sleep(ms - 1) when ms > 1, then spins on QPC
// until n ns have passed. 3.80.2 calls timeBeginPeriod(1) when its clock
// starts (0x1429BAA50), so the Sleep is accurate. VanillaFixes, which loads
// this DLL, raises this process's timer resolution to 0.5 ms, so the same is
// true here.
//
// Where to wait. 3.80.2 waits at the top of its per-frame function, before
// its frame-time step (0x1401EE610 reads the clock and dispatches the
// delta). 1.12's frame-time step is FUN_EVENT_FRAME_TIME, which runs before
// the frame pass (OnUpdate, the draw, present). So we wait at its head. Its
// clock read then includes the wait, and the world and the interface
// advance by the real frame time, as in 3.80.2. Do not sleep before Present
// as 3.3.5 / 4.3.4 do (CGxDevice::WaitForFPSCap): that shows a frame that is
// older by up to one interval. Do not wait in the present facade 0x0058A960
// either: the loading-screen repaint also calls it during a synchronous
// load, and a cap there makes the load slower.
//
// Defaults are 1.12's, which has no cap: both switches are 0 (3.80.2 ships
// useMaxFPSBk 1). maxFPS and maxFPSBk keep 3.80.2's 120 and 30, and they do
// nothing until a switch is set.
//
// Not ported:
//   - 3.80.2 also limits the rate to 60 while game state 0x1446E3148 is 0
//     (before the world). That would change 1.12's default.
//   - maxFPSLoading has only a validator (10..100, 0x1401FD0B0); nothing
//     reads its value.
//   - targetFPS / useTargetFPS lower the render scale.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "input/EventBus.h"

#include <windows.h>

#include <cfloat>
#include <cstdint>
#include <cstdlib>

namespace Gx::MaxFps {

namespace {

// 0x14243F900's "no cap", and its floor.
constexpr uint32_t kNoCap = 0xFFFFFFFFu;
constexpr int kMinFps = 8;
constexpr int64_t kNsPerSecond = 1000000000;

CVar::Factory::Handle g_useMaxFps = nullptr;
CVar::Factory::Handle g_maxFps = nullptr;
CVar::Factory::Handle g_useMaxFpsBk = nullptr;
CVar::Factory::Handle g_maxFpsBk = nullptr;

// 3.80.2's foreground flag. True until the first activation event. The
// registration sets it from the real foreground window.
bool g_foreground = true;
bool g_activateRegistered = false;

// 0x146B52EC4 (the cap the interval was computed for), 0x146B52EC8 (the
// interval), 0x146AD8100 (the deadline). All in QPC ticks.
uint32_t g_cap = 0;
int64_t g_interval = 0;
int64_t g_deadline = 0;

int64_t Qpc() {
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return v.QuadPart;
}

int64_t Frequency() {
    static const int64_t frequency = [] {
        LARGE_INTEGER v;
        QueryPerformanceFrequency(&v);
        return v.QuadPart;
    }();
    return frequency;
}

// 3.80.2's QPC-to-ns conversion. It splits the ticks into seconds and a
// remainder, so the multiply does not overflow.
int64_t ToNs(int64_t ticks) {
    const int64_t f = Frequency();
    return ticks / f * kNsPerSecond + ticks % f * kNsPerSecond / f;
}

// 0x14032E150.
void SleepNs(int64_t ns) {
    const int64_t start = ToNs(Qpc());
    const int64_t ms = ns / 1000000;
    if (ms > 1)
        Sleep(static_cast<DWORD>(ms - 1));
    while (ToNs(Qpc()) - start < ns) {
    }
}

// 0x14243F900. Unsigned like 3.80.2's, so a negative cvar value reads as a
// very large cap.
uint32_t Cap() {
    const uint32_t fg = CVar::Factory::GetInt(g_useMaxFps) != 0
                            ? static_cast<uint32_t>(CVar::Factory::GetInt(g_maxFps))
                            : kNoCap;
    uint32_t cap = CVar::Factory::GetInt(g_useMaxFpsBk) != 0
                       ? static_cast<uint32_t>(CVar::Factory::GetInt(g_maxFpsBk))
                       : kNoCap;
    if (g_foreground)
        cap = fg;
    return cap >= static_cast<uint32_t>(kMinFps) ? cap : kMinFps;
}

// The limiter in 0x1401ECA00. When there is no cap, the interval
// (freq / 0xFFFFFFFF) is 0, so no frame waits.
void Wait() {
    const uint32_t cap = Cap();
    if (cap != g_cap) {
        g_cap = cap;
        g_interval = Frequency() / cap;
        g_deadline = Qpc() + g_interval;
    }
    const int64_t now = Qpc();
    if (now < g_deadline) {
        SleepNs(ToNs(g_deadline - now));
        g_deadline += g_interval;
    } else {
        g_deadline = now + g_interval;
    }
}

using FrameTime_t = void(__fastcall *)(void *ctx, void *edx);
FrameTime_t FrameTime_o = nullptr;

// FUN_EVENT_FRAME_TIME is __thiscall: the context is in ecx. __fastcall with
// an unused edx has the same ABI.
void __fastcall FrameTime_h(void *ctx, void *edx) {
    Wait();
    FrameTime_o(ctx, edx);
}

const Game::HookAutoRegister _hookreg{Offsets::FUN_EVENT_FRAME_TIME,
                                      reinterpret_cast<void *>(&FrameTime_h),
                                      reinterpret_cast<void **>(&FrameTime_o)};

// The payload is `{ uint32_t active; }`, the low word of WM_ACTIVATE's
// wParam. Returns 1 so the dispatch continues.
int __fastcall OnActivate(const void *payload, void * /*param*/) {
    g_foreground = *static_cast<const uint32_t *>(payload) != 0;
    return 1;
}

// A value below 8 is rejected, as 3.80.2's validator does (0x14243A0E0:
// `atoi(value) > 7`).
int __fastcall ValidateFps(CVar::Factory::Handle, const char *, const char *value, void *) {
    return value != nullptr && std::atoi(value) >= kMinFps;
}

using OsGuiGetWindow_t = HWND(__fastcall *)(int which);

// The glue pass runs at every glue boot, and the first one comes before the
// world. The cvars are process-global and the frame-time hook runs from DLL
// load, so the caps also apply on the glue screens.
void RegisterGlue() {
    g_useMaxFps = CVar::Factory::Register("useMaxFPS", "Enables or disables FPS limit",
                                          Offsets::CVAR_CATEGORY_GRAPHICS, "0");
    g_maxFps = CVar::Factory::Register("maxFPS", "Set FPS limit. Min 8",
                                       Offsets::CVAR_CATEGORY_GRAPHICS, "120", &ValidateFps);
    g_useMaxFpsBk =
        CVar::Factory::Register("useMaxFPSBk", "Enables or disables background FPS limit",
                                Offsets::CVAR_CATEGORY_GRAPHICS, "0");
    g_maxFpsBk = CVar::Factory::Register("maxFPSBk", "Set background FPS limit. Min 8",
                                         Offsets::CVAR_CATEGORY_GRAPHICS, "30", &ValidateFps);

    // The event bus does not dedup, so register once. The highest priority
    // means no other handler can stop the dispatch before ours runs.
    if (g_activateRegistered)
        return;
    g_activateRegistered = true;
    Input::EventBus::Register(Offsets::EVENT_ID_ACTIVATE, &OnActivate, nullptr, FLT_MAX);
    const auto window = reinterpret_cast<OsGuiGetWindow_t>(Offsets::FUN_OS_GUI_GET_WINDOW);
    g_foreground = window(0) == window(2);
}

const Game::GlueModuleAutoRegister _autoreg{&RegisterGlue};

} // namespace

} // namespace Gx::MaxFps
