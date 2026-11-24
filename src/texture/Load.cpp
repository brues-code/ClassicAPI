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
#include <cstring>

namespace Texture {

namespace {

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
// a Pixels as the owner. The portrait's own hands its array over for every
// level, which is right only for its one-level filter; this answers each
// level at its size.
void __fastcall LatchPixels(uint32_t command, uint32_t width, uint32_t height, uint32_t /*face*/,
                            uint32_t level, void *owner, uint32_t *stride, const void **texels) {
    if (command != Offsets::GXTEX_CMD_LATCH)
        return;
    *stride = width * 4;
    *texels = static_cast<Pixels *>(owner)->Level(level, width, height);
}

} // namespace

uint32_t Flags(uint32_t filter, bool wrapU, bool wrapV) {
    uint32_t flags = 0;
    reinterpret_cast<TexFlagsInit_t>(Offsets::FUN_GX_TEXFLAGS_INIT)(&flags, filter, wrapU ? 1 : 0,
                                                                    wrapV ? 1 : 0, 0, 0, 0, 1, 0);
    return flags;
}

uint32_t DefaultFlags() {
    return Flags(Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT), false, false);
}

uint32_t SetFile(void *texture, const char *path) {
    using SetTexture_t = uint32_t(__thiscall *)(void *texture, const char *path, uint32_t wrap,
                                                uint32_t filter, uint32_t shader);
    return reinterpret_cast<SetTexture_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE)(
        texture, path, 0, Game::Read<uint32_t>(Offsets::VAR_TEXTURE_BLEND_DEFAULT), 0);
}

void *DeviceTexture(void *handle) {
    using Fn = void *(__fastcall *)(void *handle, int load, void *callback);
    return reinterpret_cast<Fn>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(handle, 1, nullptr);
}

// Same failure test and cleanup as SetTexture: a status level of 2 or more
// closes the green fallback and passes the status to the (silent)
// system-message printer; the status is destroyed either way.
void *LoadByPath(const char *path, uint32_t flags) {
    TexLoadDesc desc;
    desc.vtbl = reinterpret_cast<void *>(Offsets::PTR_TEXLOAD_DESC_VTBL);
    desc.field4 = 8;
    desc.self8 = &desc.self8;
    desc.fieldC = reinterpret_cast<uintptr_t>(&desc.self8) | 1u;
    desc.field10 = 0;
    void *handle = reinterpret_cast<void *>(reinterpret_cast<TextureLoad_t>(
        Offsets::FUN_TEXTURE_LOAD_BY_PATH)(path, &desc, flags, 0, 1));
    if (desc.field10 >= 2) {
        Game::ReleaseHandle(handle);
        handle = nullptr;
        reinterpret_cast<SysMsgAdd_t>(Offsets::FUN_SYSMSG_ADD_STATUS)(&desc, 4);
    }
    reinterpret_cast<StatusDestroy_t>(Offsets::FUN_FRAMESCRIPT_STATUS_RESET)(&desc);
    return handle;
}

// TextureCreate's candidates in its order, each resolved as its loader would
// open it; the first that resolves decides.
bool IsLooseFile(const char *path) {
    using Split_t = int(__fastcall *)(const char *name, char *buffer, char **split);
    using Resolve_t = int(__fastcall *)(const char *name, char *out, uint32_t size, uint32_t flags,
                                        uint32_t *kind, void **archive);
    if (path == nullptr || *path == '\0')
        return false;
    // The engine's buffer is TEXTURE_NAME_BUFFER; the slack keeps a name of
    // the full length from running its extension past the end.
    char name[Offsets::TEXTURE_NAME_BUFFER + 8] = {};
    char *split = nullptr;
    const int first = reinterpret_cast<Split_t>(Offsets::FUN_TEXTURE_NAME_SPLIT)(path, name, &split);
    const auto *extensions = reinterpret_cast<const char *const *>(Offsets::VAR_TEXTURE_NAME_EXTENSIONS);
    for (int i = 0; i < 2; ++i) {
        const char *extension = extensions[i == 0 ? first : 1 - first];
        std::memcpy(split, extension, std::strlen(extension) + 1);
        char resolved[Offsets::TEXTURE_NAME_BUFFER];
        uint32_t kind = 0;
        void *archive = nullptr;
        if (reinterpret_cast<Resolve_t>(Offsets::FUN_SFILE_RESOLVE_OPEN)(
                name, resolved, sizeof resolved, Offsets::SFILE_OPEN_LOOSE, &kind, &archive) != 0)
            return kind != Offsets::SFILE_KIND_ARCHIVE;
    }
    return false;
}

const uint32_t *Pixels::Level(uint32_t slot, uint32_t width, uint32_t height) {
    if ((width == width_ && height == height_) || width == 0 || height == 0)
        return texels_;
    std::vector<uint32_t> &out = levels_[slot < kSlots ? slot : kSlots - 1];
    out.resize(size_t{width} * height);
    // Each texel averages the block of full-size texels it covers, per
    // channel, alpha included.
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t y0 = y * height_ / height;
        const uint32_t y1 = (y + 1) * height_ / height > y0 ? (y + 1) * height_ / height : y0 + 1;
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t x0 = x * width_ / width;
            const uint32_t x1 = (x + 1) * width_ / width > x0 ? (x + 1) * width_ / width : x0 + 1;
            uint32_t sum[4] = {};
            for (uint32_t sy = y0; sy < y1; ++sy)
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    const uint32_t c = texels_[size_t{sy} * width_ + sx];
                    for (int k = 0; k < 4; ++k)
                        sum[k] += (c >> (8 * k)) & 0xFF;
                }
            const uint32_t n = (y1 - y0) * (x1 - x0);
            uint32_t c = 0;
            for (int k = 0; k < 4; ++k)
                c |= ((sum[k] + n / 2) / n) << (8 * k);
            out[size_t{y} * width + x] = c;
        }
    }
    return out.data();
}

void *CreateFromPixels(const char *name, Pixels &pixels, uint32_t flags) {
    return reinterpret_cast<CreateWithCallback_t>(Offsets::FUN_TEXTURE_CREATE_WITH_CALLBACK)(
        pixels.Width(), pixels.Height(), 1, Offsets::GXTEX_FORMAT_ARGB8888, flags, &pixels,
        reinterpret_cast<void *>(&LatchPixels), name, 0);
}

void *CreateSolid(uint32_t bgra) {
    using CreateSolid_t = void *(__fastcall *)(const uint32_t *bgra, uint32_t unused);
    return reinterpret_cast<CreateSolid_t>(Offsets::FUN_TEXTURE_CREATE_SOLID)(&bgra, 0);
}

} // namespace Texture
