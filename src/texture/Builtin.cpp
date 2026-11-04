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

// BLPs the later clients ship and 1.12 does not, served to every texture load.
//
// A texture under the embedded addon's path would not load: BLPs open through
// SFile, not the read the addon redirects. So the image is texels in the DLL,
// handed out by TextureCreateFromBlp when SFile has no such file. TextureCreate
// (FUN_TEXTURE_LOAD_BY_PATH) reaches the BLP loader only on a miss in its
// texture hash and inserts what it returns under the name and flags, so the
// engine's own cache deduplicates and reference-counts the result like any
// file's. A real file of the same name (a patch MPQ, a loose file) still wins.
//
// The texels come from two places:
//   - interface\cooldown\edge.blp is built here from its decoded texels;
//   - the files under src/embed/Textures sit at their game paths
//     (Texture::Builtin::Manifest), each the later client's own BLP. One
//     decodes the first time a load or Find asks for it, through the engine's
//     BLP reader on the DLL's copy (Texture::BlpReader), and its texels stay.

#include "texture/Builtin.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "embed/Resource.h"
#include "embedded_resources.h"
#include "texture/BlpReader.h"
#include "texture/Load.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Texture::Builtin {

namespace {

// interface\cooldown\edge.blp (4.3.4): 16x16 DXT5, decoded the standard way
// (565 widened by bit replication), as 4.3.4's device samples it. Each of rows
// 0-7 is one colour with alpha only in column 8, a line from the centre to the
// top edge fading out; rows 8-15 are transparent black.
constexpr uint32_t kEdgeSize = 16;
constexpr uint32_t kEdgeColumn = 8;
const uint32_t kEdgeRowColor[8] = {0xFFD300, 0xFFD300, 0xFFD300, 0xDEBA00,
                                   0xB59200, 0x806900, 0x4C4000, 0x181800};
const uint8_t kEdgeColumnAlpha[8] = {0xFF, 0xEB, 0xC9, 0xA7, 0x7E, 0x5A, 0x36, 0x12};

// Outlives every handle: the engine reads it whenever it uploads one.
struct EdgeTexels {
    uint32_t texels[kEdgeSize * kEdgeSize] = {};
    EdgeTexels() {
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < kEdgeSize; ++x)
                texels[y * kEdgeSize + x] =
                    kEdgeRowColor[y] | (x == kEdgeColumn ? uint32_t{kEdgeColumnAlpha[y]} << 24 : 0);
    }
};
const EdgeTexels g_edge;

// One embedded file, decoded at most once. Like the edge texels, the texels
// outlive every handle made from them.
struct Embedded {
    bool tried = false;
    std::vector<uint32_t> texels;
    Image image = {};
};
Embedded g_embedded[Manifest::kFileCount];

void Decode(const Manifest::File &file, Embedded &out) {
    const Embed::Bytes bytes = Embed::File(file.resourceId, static_cast<size_t>(file.index));
    BlpReader blp(bytes.data, bytes.size);
    const uint8_t *bgra = blp.DecodeBgra();
    if (bgra == nullptr)
        return;
    out.texels.resize(size_t{blp.Width()} * blp.Height());
    std::memcpy(out.texels.data(), bgra, out.texels.size() * sizeof(uint32_t));
    out.image = {blp.Width(), blp.Height(), out.texels.data()};
}

using CreateFromBlp_t = void *(__fastcall *)(const char *path, uint32_t fileFlag, uint32_t flags,
                                             void *log);
CreateFromBlp_t CreateFromBlp_o = nullptr;

void *__fastcall CreateFromBlp_h(const char *path, uint32_t fileFlag, uint32_t flags, void *log) {
    void *handle = CreateFromBlp_o(path, fileFlag, flags, log);
    if (handle != nullptr || path == nullptr)
        return handle;
    if (Common::SamePath(path, "interface\\cooldown\\edge.blp"))
        return CreateFromPixels(path, kEdgeSize, kEdgeSize, g_edge.texels, flags);
    if (const Image *image = Find(path))
        return CreateFromPixels(path, image->width, image->height, image->texels, flags);
    return nullptr;
}

const Game::HookAutoRegister _createFromBlpHook{Offsets::FUN_TEXTURE_CREATE_FROM_BLP,
                                                reinterpret_cast<void *>(&CreateFromBlp_h),
                                                reinterpret_cast<void **>(&CreateFromBlp_o)};

} // namespace

const Image *Find(const char *path) {
    if (path == nullptr)
        return nullptr;
    const Manifest::File *file = Embed::FindFile(Manifest::kFiles, path);
    if (file == nullptr)
        return nullptr;
    Embedded &embedded = g_embedded[file - Manifest::kFiles];
    if (!embedded.tried) {
        embedded.tried = true;
        Decode(*file, embedded);
    }
    return embedded.texels.empty() ? nullptr : &embedded.image;
}

} // namespace Texture::Builtin
