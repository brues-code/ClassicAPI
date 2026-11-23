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

// `maxFPSLoading`: how often the loading screen is redrawn while something
// loads, ported from the 3.80.2 Classic client (WowClassicGM-China.exe
// 3.80.2.69496).
//
// Each progress setter on both clients keeps the largest fraction it was
// given and then repaints the whole loading screen. 1.12's repaint
// (FUN_LOADING_SCREEN_REPAINT) always draws and presents, and with vsync each
// present waits for the next refresh. The UI load calls its setter after
// every TOC file, so 208 files cost more than three seconds of presents
// against 70 ms of Lua.
//
// 3.80.2's repaint, UpdateProgressBar (0x1401FD760), takes a `final` flag and
// draws a call that is not final only when more than 1000 / maxFPSLoading ms
// have passed since its last draw. The cvar is clamped to 10..100 there, and
// a missing cvar means 100 ms. Its callers pass `final` when their own input
// has reached 1.0: the UI-load callback 0x140202970 and the player-entry loop
// in 0x141420B10. The cvar (0x140126C60) defaults to 10, so a load is redrawn
// about ten times a second, and its validator (0x1401FD0B0) rejects anything
// outside 10..100.
//
// 1.12 has three setters with the same shape (async loads, the map load, the
// UI load). Each calls the repaint from one call site, so each site is
// redirected here with its own input, and `final` is that input == 1.0, as in
// 3.80.2. The fourth caller, the raise, is left alone: 3.80.2 does not raise
// its screen through UpdateProgressBar either. A skipped repaint loses
// nothing, because the setter has already stored the fraction and the next
// drawn repaint shows it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "time/Clock.h"

#include <cstdint>
#include <cstdlib>

namespace Gx::MaxFpsLoading {

namespace {

constexpr uint32_t kMinFps = 10;
constexpr uint32_t kMaxFps = 100;
constexpr uint32_t kIntervalWithoutCvar = 100; // ms

CVar::Factory::Handle g_maxFpsLoading = nullptr;
uint32_t g_lastDraw = 0; // 0x1466601F4

using Repaint_t = void(__cdecl *)();

// The gate at the top of 0x1401FD760.
void Repaint(uintptr_t input) {
    const uint32_t now = Time::Clock::NowMs();
    if (Game::Read<float>(input) != 1.0f) {
        uint32_t interval = kIntervalWithoutCvar;
        if (g_maxFpsLoading != nullptr) {
            uint32_t fps = static_cast<uint32_t>(CVar::Factory::GetInt(g_maxFpsLoading));
            fps = fps < kMaxFps ? fps : kMaxFps;
            fps = fps > kMinFps ? fps : kMinFps;
            interval = 1000 / fps;
        }
        if (Time::Clock::Elapsed(g_lastDraw, now) <= interval)
            return;
    }
    g_lastDraw = now;
    reinterpret_cast<Repaint_t>(Offsets::FUN_LOADING_SCREEN_REPAINT)();
}

void __cdecl RepaintAsync() { Repaint(Offsets::VAR_LOADING_PROGRESS_ASYNC); }
void __cdecl RepaintMap() { Repaint(Offsets::VAR_LOADING_PROGRESS_MAP); }
void __cdecl RepaintUi() { Repaint(Offsets::VAR_LOADING_PROGRESS_UI); }

// 0x1401FD0B0: strtol in base 16 after "0x" / "0X", else base 10; a value
// outside 10..100 prints the error to the console and is rejected.
int __fastcall Validate(CVar::Factory::Handle, const char *, const char *value, void *) {
    const int base = value != nullptr && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')
                         ? 16
                         : 10;
    const long fps = value != nullptr ? std::strtol(value, nullptr, base) : 0;
    if (fps < static_cast<long>(kMinFps) || fps > static_cast<long>(kMaxFps)) {
        Game::Console::Printf("MaxFPSLoading must be between %d and %d", static_cast<int>(kMinFps),
                              static_cast<int>(kMaxFps));
        return 0;
    }
    return 1;
}

// The glue pass runs at every glue boot, and the first one comes before the
// first loading screen. Both are idempotent.
void RegisterGlue() {
    g_maxFpsLoading = CVar::Factory::Register("maxFPSLoading", "Set loading screen max FPS",
                                              Offsets::CVAR_CATEGORY_GRAPHICS, "10", &Validate);
    const auto repaint = static_cast<uintptr_t>(Offsets::FUN_LOADING_SCREEN_REPAINT);
    Common::RedirectCall(Offsets::SITE_LOADING_PROGRESS_ASYNC_REPAINT, repaint,
                         reinterpret_cast<const void *>(&RepaintAsync));
    Common::RedirectCall(Offsets::SITE_LOADING_PROGRESS_MAP_REPAINT, repaint,
                         reinterpret_cast<const void *>(&RepaintMap));
    Common::RedirectCall(Offsets::SITE_LOADING_PROGRESS_UI_REPAINT, repaint,
                         reinterpret_cast<const void *>(&RepaintUi));
}

const Game::GlueModuleAutoRegister _autoreg{&RegisterGlue};

} // namespace

} // namespace Gx::MaxFpsLoading
