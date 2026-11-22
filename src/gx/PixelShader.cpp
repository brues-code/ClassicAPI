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

// The engine's loader (FUN_GX_SHADER_CREATE -> device vmethod +0xDC) dedups by
// path in the device's shader table, opens the file with the handle API (a
// missing file is a silent return) and compiles what it read. For a path with
// no file the node comes back registered and empty; we fill the fields the
// file parser would have (the code array through the engine's own grow, the
// size, the code type, valid) and compile it the way the loader does.

#include "gx/PixelShader.h"

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"

#include <cstring>

namespace Gx::PixelShader {

namespace {

using ShaderCreate_t = void(__fastcall *)(int type, void **outSlot, const char *path);
using ArrayGrow_t = void(__thiscall *)(void *desc, uint32_t newCap);
using CompilePs_t = void(__thiscall *)(void *device, void *shader);

void *Device() { return Game::Read<void *>(Offsets::VAR_GX_DEVICE); }

// The engine swallows CreatePixelShader's HRESULT: ask the device with the
// same bytes so the log carries the reason (D3DERR_INVALIDCALL 0x8876086C =
// the runtime validator refused it).
long Diagnose(void *dev, const uint32_t *tokens) {
    using CreatePs_t = long(__stdcall *)(void *device, const uint32_t *code, void **out);
    using Release_t = unsigned long(__stdcall *)(void *object);
    void *d3d = Game::Read<void *>(dev, Offsets::OFF_GXDEVD3D_DEVICE9);
    if (d3d == nullptr)
        return 0;
    void **vtbl = *reinterpret_cast<void ***>(d3d);
    void *ps = nullptr;
    const long hr = reinterpret_cast<CreatePs_t>(vtbl[106])(d3d, tokens, &ps);
    if (ps != nullptr)
        reinterpret_cast<Release_t>(vtbl[2])(ps);
    return hr;
}

} // namespace

int Profile() {
    void *dev = Device();
    return dev != nullptr ? Game::Read<int>(dev, Offsets::OFF_GXDEV_PS_PROFILE) : -1;
}

void *Build(const char *path, const uint32_t *tokens, uint32_t count, const char *what, Result &result) {
    void *dev = Device();
    // A lost device (a failed Present; fullscreen alt-tab) fails every compile
    // until the engine resets it, so a build now proves nothing.
    if (dev == nullptr || Game::Read<int>(dev, Offsets::OFF_GXDEVD3D_ALIVE) == 0) {
        result = Result::Retry;
        return nullptr;
    }
    void *node = nullptr;
    reinterpret_cast<ShaderCreate_t>(Offsets::FUN_GX_SHADER_CREATE)(Offsets::GXSHADER_TYPE_PIXEL, &node, path);
    if (node == nullptr) {
        Debug::Log::Printf("[%s] the shader loader returned no node for %s", what, path);
        result = Result::Rejected;
        return nullptr;
    }
    if (Game::Read<uint32_t>(node, Offsets::OFF_GXSHADER_CODE_SIZE) == 0) {
        const uint32_t bytes = count * sizeof(uint32_t);
        reinterpret_cast<ArrayGrow_t>(Offsets::FUN_GX_ARRAY_GROW_BYTES)(
            Game::Ptr<uint8_t>(node, Offsets::OFF_GXSHADER_CODE_DESC), bytes);
        uint8_t *data = Game::Read<uint8_t *>(node, Offsets::OFF_GXSHADER_CODE_DATA);
        if (data == nullptr) {
            Debug::Log::Printf("[%s] the code-array grow returned no buffer for %s", what, path);
            result = Result::Rejected;
            return nullptr;
        }
        std::memcpy(data, tokens, bytes);
        Game::Ref<uint32_t>(node, Offsets::OFF_GXSHADER_CODE_SIZE) = bytes;
        Game::Ref<int>(node, Offsets::OFF_GXSHADER_CODE_TYPE) = Offsets::GXSHADER_CODE_D3D;
        Game::Ref<int>(node, Offsets::OFF_GXSHADER_VALID) = 1;
        reinterpret_cast<CompilePs_t>(Offsets::FUN_GX_D3D_COMPILE_PIXEL_SHADER)(dev, node);
        if (Game::Read<void *>(node, Offsets::OFF_GXSHADER_D3D_OBJECT) == nullptr)
            Debug::Log::Printf("[%s] CreatePixelShader rejected %s (hr=0x%08lX)", what, path, Diagnose(dev, tokens));
    }
    if (Game::Read<int>(node, Offsets::OFF_GXSHADER_VALID) == 0 ||
        Game::Read<void *>(node, Offsets::OFF_GXSHADER_D3D_OBJECT) == nullptr) {
        result = Result::Rejected;
        return nullptr;
    }
    result = Result::Built;
    return node;
}

} // namespace Gx::PixelShader
