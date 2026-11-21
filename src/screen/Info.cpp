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

// `GetPhysicalScreenSize()` → (widthPixels, heightPixels) — the size of the
// back buffer the game renders into. FrameXML's `PixelUtil` hangs its
// pixel↔UI-unit conversion off it (`768.0 / physicalHeight`), which is what
// lets addons snap frames to whole pixels.
//
// 3.80.2 (FUN_141b6ddb0) returns its swapchain's back-buffer size; when that
// is 0 wide, the window's client size; with no swapchain, the size the
// swapchain is to be created with. Always whole numbers.
//
// 1.12 has no swapchain object, so this reads the back-buffer size the way
// the device sizes its back buffer (D3D BuildPresentParameters 0x00599790):
// windowed, the window rectangle's right and bottom edges truncated
// (__ftol); full screen, the format's size. The window rectangle is what
// SetWindowRects stores on each resize, so this follows a resized window.
// An empty rectangle falls back to the format's size, the size the device
// was asked for — 3.80.2's last fallback.
//
// The window rectangle, not GxCapsWindowSize: that returns the draw
// rectangle, which is a render target's size while one is bound.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Screen::Info {

namespace {

int __fastcall Script_GetPhysicalScreenSize(void *L) {
    const auto *dev = Game::Read<const uint8_t *>(Offsets::VAR_GX_DEVICE);
    const uint8_t *format = dev + Offsets::OFF_GXDEV_FORMAT;
    int width = 0;
    int height = 0;
    if (Game::Read<uint8_t>(format, Offsets::OFF_GXFORMAT_WINDOWED) != 0) {
        const auto *rect = reinterpret_cast<const float *>(dev + Offsets::OFF_GXDEV_WINDOW_RECT);
        width = static_cast<int>(rect[3]);
        height = static_cast<int>(rect[2]);
    }
    if (width == 0) {
        width = Game::Read<int32_t>(format, Offsets::OFF_GXFORMAT_WIDTH);
        height = Game::Read<int32_t>(format, Offsets::OFF_GXFORMAT_HEIGHT);
    }
    Game::Lua::PushNumber(L, static_cast<double>(width));
    Game::Lua::PushNumber(L, static_cast<double>(height));
    return 2;
}

const Game::Doc::Field kGetPhysicalScreenSizeRets[] = {
    Game::Doc::Req("sizeX", "number"),
    Game::Doc::Req("sizeY", "number"),
};
const Game::Doc::Function kGetPhysicalScreenSize{
    "Returns the size of the game window in pixels.", {}, kGetPhysicalScreenSizeRets,
    "SystemGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetPhysicalScreenSize",
                                      &Script_GetPhysicalScreenSize, &kGetPhysicalScreenSize);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Screen::Info
