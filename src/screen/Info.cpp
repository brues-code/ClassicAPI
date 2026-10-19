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

// `GetPhysicalScreenSize()` → (widthPixels, heightPixels) — the size of
// the game's render window in pixels. FrameXML's `PixelUtil` hangs its
// pixel↔UI-unit conversion off it (`768.0 / physicalHeight`), which is
// what lets addons snap frames to whole pixels.
//
// The size comes from the graphics device: GxCapsWindowSize
// (FUN_GX_CAPS_WINDOW_SIZE), the live render-window rectangle. It is the
// engine's own answer to "how many pixels is the screen" — its font code
// rasterizes text at exactly this size (GxuFontUpdateViewport), and it
// follows a resized window, unlike the configured `gxResolution` CVar.
//
// An empty rectangle (no device yet) gives 640x480, the font code's own
// fallback.

#include "Game.h"
#include "Offsets.h"

namespace Screen::Info {

namespace {

using GxCapsWindowSize_t = void(__fastcall *)(float *rect);

int __fastcall Script_GetPhysicalScreenSize(void *L) {
    float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    reinterpret_cast<GxCapsWindowSize_t>(Offsets::FUN_GX_CAPS_WINDOW_SIZE)(rect);
    double width = rect[3] - rect[1];
    double height = rect[2] - rect[0];
    if (width == 0.0 || height == 0.0) {
        width = 640.0;
        height = 480.0;
    }
    Game::Lua::PushNumber(L, width);
    Game::Lua::PushNumber(L, height);
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetPhysicalScreenSize",
                                      &Script_GetPhysicalScreenSize);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Screen::Info
