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

#include "texture/Load.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Texture {

namespace {

using HandleRelease_t = void(__fastcall *)(void *handle);
using SysMsgAdd_t = int(__fastcall *)(void *status, uint32_t level);
using StatusDestroy_t = void(__thiscall *)(void *status);
using TexFlagsInit_t = void *(__thiscall *)(void *self, uint32_t blend, int, int, int, int, int,
                                            uint32_t, int);
using TextureLoad_t = uint32_t(__fastcall *)(const char *path, void *desc, uint32_t flags, int,
                                             int);

using CreateWithCallback_t = void *(__fastcall *)(uint32_t w, uint32_t h, uint32_t mips,
                                                  uint32_t format, uint32_t flags, void *owner,
                                                  void *callback, const char *name, int force);

struct TexLoadDesc {
    void *vtbl;
    int32_t field4;
    void *self8;
    uint32_t fieldC;
    int32_t field10;
};

// The texel source of CreateFromPixels, the portrait cache's 0x00525AE0 with
// the array as the owner.
void __fastcall LatchPixels(uint32_t command, uint32_t width, uint32_t /*height*/,
                            uint32_t /*face*/, uint32_t /*level*/, void *owner, uint32_t *stride,
                            const void **texels) {
    if (command != Offsets::GXTEX_CMD_LATCH)
        return;
    *stride = width * 4;
    *texels = owner;
}

} // namespace

uint32_t DefaultFlags() {
    uint32_t flags = 0;
    const uint32_t blend = Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT);
    reinterpret_cast<TexFlagsInit_t>(Offsets::FUN_GX_TEXFLAGS_INIT)(&flags, blend, 0, 0, 0, 0, 0,
                                                                    1, 0);
    return flags;
}

void ReleaseHandle(void *handle) {
    if (handle != nullptr)
        reinterpret_cast<HandleRelease_t>(Offsets::FUN_HANDLE_RELEASE)(handle);
}

// Same failure test and cleanup as SetTexture: a status level of 2 or more
// closes the green fallback and passes the status to the (silent)
// system-message printer; the status is destroyed either way.
void *LoadByPath(const char *path) {
    TexLoadDesc desc;
    desc.vtbl = reinterpret_cast<void *>(Offsets::PTR_TEXLOAD_DESC_VTBL);
    desc.field4 = 8;
    desc.self8 = &desc.self8;
    desc.fieldC = reinterpret_cast<uintptr_t>(&desc.self8) | 1u;
    desc.field10 = 0;
    void *handle = reinterpret_cast<void *>(reinterpret_cast<TextureLoad_t>(
        Offsets::FUN_TEXTURE_LOAD_BY_PATH)(path, &desc, DefaultFlags(), 0, 1));
    if (desc.field10 >= 2) {
        ReleaseHandle(handle);
        handle = nullptr;
        reinterpret_cast<SysMsgAdd_t>(Offsets::FUN_SYSMSG_ADD_STATUS)(&desc, 4);
    }
    reinterpret_cast<StatusDestroy_t>(Offsets::FUN_FRAMESCRIPT_STATUS_RESET)(&desc);
    return handle;
}

void *CreateFromPixels(const char *name, uint32_t width, uint32_t height,
                       const uint32_t *texels, uint32_t flags) {
    return reinterpret_cast<CreateWithCallback_t>(Offsets::FUN_TEXTURE_CREATE_WITH_CALLBACK)(
        width, height, 1, Offsets::GXTEX_FORMAT_ARGB8888, flags,
        const_cast<uint32_t *>(texels), reinterpret_cast<void *>(&LatchPixels), name, 0);
}

void *CreateSolid(uint32_t bgra) {
    using CreateSolid_t = void *(__fastcall *)(const uint32_t *bgra, uint32_t unused);
    return reinterpret_cast<CreateSolid_t>(Offsets::FUN_TEXTURE_CREATE_SOLID)(&bgra, 0);
}

} // namespace Texture
