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

// A ModelScene's terrain (scene:SetWorldTerrain, a ClassicAPI extension;
// research in docs/notes/model-scene.md "Extension 9").
//
// The chunks are the world's CMapChunk objects, owned by the scene, built
// and drawn by the world's own functions:
//
//   - Load. The ADT is read whole into the scene's buffer and its MHDR read
//     as ParseAreaFile (0x006C2010) reads it. The tile's textures load into
//     an area stand-in through LoadAreaTextures, which touches only the
//     texture slots. Each chunk is constructed by CMapChunk's constructor
//     and gets CreateAreaChunk's fields (0x006C2880) and the self-contained
//     steps of CMapChunk::Load (0x006AF5F0) in its order: record fixup,
//     header fields, vertices, normals, the impassable flag, layers, shadow
//     texture, combined texture, flags. Load's other steps write world
//     state and are left out: LinkNeighbours reads the global area table
//     (the scene links its own chunks by the same rule), LoadReferences
//     makes doodad and map object defs in the world's hashes, LinkLights
//     takes the world's map lights, and the area is never filed in the
//     global table. Liquids and sound emitters (a null callback in 1.12)
//     are left out too.
//   - Buffers. The world's chunk buffer pool is freed and rebuilt on a far
//     clip change and a map unload (CreateChunkBufferPool 0x006AE640), and
//     only the world's listed chunks give theirs back first. The scene's
//     chunks take their buffers from a static pool of their own, made as
//     the world makes its pool; SelectMesh goes to the world's pool only for
//     a buffer of the device's dynamic pool (usage 2).
//   - Draw. DrawChunkPass (0x00684510) for each chunk the scene's view
//     shows: the chunk level and view depth (0x006AFAD0), the box against
//     the scene's volume (CullSceneChunks 0x00683BF0, without the horizon),
//     the world transform at the chunk less the eye, a lighting record at
//     the chunk's centre with the scene's lights there, the fog and, nearer
//     than the fog end by twice the radius, the light (the chunk's slots
//     0x006C0CC0: the scene light stands in for the sun, the scene fog for
//     the zone's), then SelectMesh. DrawLayers reads the camera and the fog
//     end from world globals, so both hold the scene's values while it draws.
//   - Settings. A chunk's textures are made for the shader flags, the
//     shadowLevel and the map shadows flag it was built with; when any
//     changes (a world loads after the glue screen), the chunks are built
//     again from the kept files.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "gx/Device.h"
#include "gx/Matrix.h"
#include "model/M2.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace Model::Scene::Terrain {

namespace {

using Gx::Identity;
using Gx::Matrix;
using Gx::Translate;

constexpr int kWorldStack = 8;
constexpr char kVertexPoolName[] = "ModelScene_terrain_vtx";
constexpr char kIndexPoolName[] = "ModelScene_terrain_idx";

struct alignas(16) ChunkMemory {
    uint8_t bytes[Offsets::CMAPCHUNK_SIZE];
};

struct alignas(16) AreaMemory {
    uint8_t bytes[Offsets::CMAPAREA_SIZE];
};

uint32_t U32(const uint8_t *at) {
    uint32_t v;
    std::memcpy(&v, at, sizeof v);
    return v;
}

float F32(const uint8_t *at) {
    float v;
    std::memcpy(&v, at, sizeof v);
    return v;
}

// The whole file, through SFile (a missing file opens nothing, no error).
bool ReadFile(const char *path, std::vector<uint8_t> &out) {
    using Open_t = int(__stdcall *)(const char *path, void **file);
    using Size_t = uint32_t(__stdcall *)(void *file, uint32_t *high);
    using Read_t = int(__stdcall *)(void *file, void *buffer, uint32_t size, uint32_t *read, void *overlapped, int);
    using Close_t = uint32_t(__stdcall *)(void *file);
    void *file = nullptr;
    if (reinterpret_cast<Open_t>(Offsets::FUN_SFILE_OPEN)(path, &file) == 0 || file == nullptr)
        return false;
    const uint32_t size = reinterpret_cast<Size_t>(Offsets::FUN_SFILE_GET_FILE_SIZE)(file, nullptr);
    bool ok = size != 0 && size != 0xFFFFFFFFu;
    if (ok) {
        out.resize(size);
        uint32_t read = 0;
        ok = reinterpret_cast<Read_t>(Offsets::FUN_SFILE_READ)(file, out.data(), size, &read, nullptr, 0) != 0 &&
             read == size;
    }
    reinterpret_cast<Close_t>(Offsets::FUN_SFILE_CLOSE)(file);
    return ok;
}

// What a chunk's textures were made for (AddLayer, CreateShadowTexture,
// CreateCombinedTexture, LoadAreaTextures).
struct Settings {
    uint32_t shader = 0;
    uint32_t shaderB = 0;
    uint32_t shadowLevel = 0;
    uint32_t mapShadows = 0;

    static Settings Current() {
        Settings s;
        s.shader = Game::Read<uint32_t>(Offsets::VAR_TERRAIN_SHADER_FLAG);
        s.shaderB = Game::Read<uint32_t>(Offsets::VAR_TERRAIN_SHADER_FLAG_B);
        s.shadowLevel = Game::Read<uint32_t>(Offsets::VAR_SHADOW_LEVEL);
        s.mapShadows = Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS) & Offsets::WORLD_FLAG_MAP_SHADOWS;
        return s;
    }
    bool operator==(const Settings &o) const {
        return shader == o.shader && shaderB == o.shaderB && shadowLevel == o.shadowLevel &&
               mapShadows == o.mapShadows;
    }
};

// One ADT: its bytes (every chunk's record and the fill callbacks' alpha
// and shadow maps point into them), the MTEX names and the area stand-in
// that holds the tile's texture handles.
struct Tile {
    std::vector<uint8_t> file;
    uint32_t column = 0;
    uint32_t row = 0;
    const char *names = nullptr;
    uint32_t namesSize = 0;
    std::unique_ptr<AreaMemory> area;
    // The MCIN indices of the chunks inside the radius.
    std::vector<uint32_t> wanted;
};

struct Chunk {
    std::unique_ptr<ChunkMemory> memory;
    Tile *tile = nullptr;
    uint32_t index = 0; // row * 16 + column in the tile
    void *object() { return memory->bytes; }
};

} // namespace

struct Land {
    std::vector<std::unique_ptr<Tile>> tiles;
    std::vector<Chunk> chunks;
    void *vertexPool = nullptr;
    void *indexPool = nullptr;
    std::vector<std::pair<void *, void *>> buffers; // one pair per chunk, in order
    Settings built;
};

namespace {

uint32_t LevelCount(uintptr_t lists, uint32_t level) {
    return Game::Read<uint32_t>(lists + level * Offsets::CHUNK_LEVEL_LIST_SIZE);
}

uint32_t FullVertexCount() { return LevelCount(Offsets::VAR_CHUNK_LEVEL_VERTEX_LISTS, Offsets::CHUNK_LEVEL_FULL); }
uint32_t FullIndexCount() { return LevelCount(Offsets::VAR_CHUNK_LEVEL_INDEX_LISTS, Offsets::CHUNK_LEVEL_FULL); }

// A byte of the file at an offset already checked to lie inside it.
const uint8_t *At(const std::vector<uint8_t> &file, uint64_t offset) {
    return file.data() + static_cast<size_t>(offset);
}

bool Inside(const std::vector<uint8_t> &file, uint64_t offset, uint64_t size) {
    return offset <= file.size() && size <= file.size() - offset;
}

// MHDR as ParseAreaFile reads it; false when a table lies outside the file
// or the tile has more names than the area holds.
bool ParseHeader(Tile &t) {
    const std::vector<uint8_t> &f = t.file;
    if (!Inside(f, Offsets::OFF_ADT_MVER_SIZE, 4))
        return false;
    const uint64_t mhdr = uint64_t{Offsets::ADT_MHDR_BASE} + U32(At(f, Offsets::OFF_ADT_MVER_SIZE));
    if (!Inside(f, mhdr, Offsets::OFF_MHDR_MTEX + 4))
        return false;
    const uint64_t mcin = mhdr + U32(At(f, mhdr + Offsets::OFF_MHDR_MCIN)) + Offsets::MCNK_RECORD_HEADER;
    const uint64_t mtex = mhdr + U32(At(f, mhdr + Offsets::OFF_MHDR_MTEX)) + Offsets::MCNK_RECORD_HEADER;
    if (!Inside(f, mcin, uint64_t{Offsets::ADT_CHUNKS} * Offsets::MCIN_ENTRY_SIZE) || mtex < 4 || !Inside(f, mtex, 0))
        return false;
    const uint32_t size = U32(At(f, mtex - 4));
    if (!Inside(f, mtex, size))
        return false;
    // LoadAreaTextures' walk: a name, then past its NUL, while inside the size.
    uint32_t count = 0;
    for (uint32_t i = 0; i < size; ++i) {
        ++count;
        while (i < size && *At(f, mtex + i) != 0)
            ++i;
    }
    if (count > Offsets::CMAPAREA_TEXTURE_SLOTS || (size != 0 && *At(f, mtex + size - 1) != 0))
        return false;
    t.names = reinterpret_cast<const char *>(At(f, mtex));
    t.namesSize = size;
    return true;
}

const uint8_t *McinEntry(const Tile &t, uint32_t index) {
    const uint64_t mhdr = uint64_t{Offsets::ADT_MHDR_BASE} + U32(At(t.file, Offsets::OFF_ADT_MVER_SIZE));
    const uint64_t mcin = mhdr + U32(At(t.file, mhdr + Offsets::OFF_MHDR_MCIN)) + Offsets::MCNK_RECORD_HEADER;
    return At(t.file, mcin + uint64_t{index} * Offsets::MCIN_ENTRY_SIZE);
}

// The MCNK record of a chunk, or null when the record or a sub-record the
// build reads lies outside the file, or a layer names a texture the tile
// does not have.
const uint8_t *CheckedRecord(const Tile &t, uint32_t index, uint32_t textures) {
    const std::vector<uint8_t> &f = t.file;
    const uint32_t record = U32(McinEntry(t, index));
    if (!Inside(f, record, uint64_t{Offsets::MCNK_RECORD_HEADER} + Offsets::MCNK_HEADER_SIZE))
        return nullptr;
    const uint8_t *header = At(f, record + Offsets::MCNK_RECORD_HEADER);
    const auto sub = [&](uint32_t field, uint64_t size) {
        return Inside(f, uint64_t{record} + U32(header + field) + Offsets::MCNK_RECORD_HEADER, size);
    };
    const uint32_t layers = U32(header + Offsets::OFF_MCNK_LAYER_COUNT);
    if (!sub(Offsets::OFF_MCNK_MCVT, Offsets::MCVT_SIZE) || !sub(Offsets::OFF_MCNK_MCNR, Offsets::MCNR_SIZE) ||
        layers > Offsets::MCNK_MAX_LAYERS || !sub(Offsets::OFF_MCNK_MCLY, uint64_t{layers} * Offsets::MCLY_ENTRY_SIZE))
        return nullptr;
    if ((U32(header + Offsets::OFF_MCNK_FLAGS) & Offsets::MCNK_FLAG_SHADOW) != 0 &&
        !sub(Offsets::OFF_MCNK_MCSH, Offsets::MCSH_SIZE))
        return nullptr;
    const uint64_t mcly = uint64_t{record} + U32(header + Offsets::OFF_MCNK_MCLY) + Offsets::MCNK_RECORD_HEADER;
    const uint64_t mcal = uint64_t{record} + U32(header + Offsets::OFF_MCNK_MCAL) + Offsets::MCNK_RECORD_HEADER;
    for (uint32_t i = 0; i < layers; ++i) {
        const uint8_t *entry = At(f, mcly + uint64_t{i} * Offsets::MCLY_ENTRY_SIZE);
        if (U32(entry) >= textures)
            return nullptr;
        if ((U32(entry + Offsets::OFF_MCLY_FLAGS) & Offsets::MCLY_FLAG_ALPHA) != 0 &&
            !Inside(f, mcal + U32(entry + Offsets::OFF_MCLY_ALPHA), Offsets::MCAL_MAP_SIZE))
            return nullptr;
    }
    return At(f, record);
}

uint32_t TextureCount(const Tile &t) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < t.namesSize; ++i) {
        ++count;
        while (i < t.namesSize && t.names[i] != 0)
            ++i;
    }
    return count;
}

void LoadTextures(Tile &t) {
    t.area = std::make_unique<AreaMemory>();
    std::memset(t.area->bytes, 0, sizeof t.area->bytes);
    using Load_t = void(__thiscall *)(void *area, const char *names, uint32_t size);
    reinterpret_cast<Load_t>(Offsets::FUN_LOAD_AREA_TEXTURES)(t.area->bytes, t.names, t.namesSize);
}

// UnloadArea's texture half (0x006A6AA0).
void CloseTextures(Tile &t) {
    if (t.area == nullptr)
        return;
    uint8_t *area = t.area->bytes;
    const uint32_t count = Game::Read<uint32_t>(area, Offsets::OFF_CMAPAREA_TEXTURE_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        void *&handle = Game::Ref<void *>(area, Offsets::OFF_CMAPAREA_TEXTURES + i * 4);
        if (handle != nullptr) {
            reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_HANDLE_RELEASE)(handle);
            handle = nullptr;
        }
    }
    Game::Ref<uint32_t>(area, Offsets::OFF_CMAPAREA_TEXTURE_COUNT) = 0;
    t.area.reset();
}

// CreateAreaChunk's fields, then CMapChunk::Load's self-contained steps.
void BuildChunk(Chunk &c) {
    using This_t = void(__thiscall *)(void *);
    void *o = c.object();
    std::memset(o, 0, Offsets::CMAPCHUNK_SIZE);
    reinterpret_cast<void *(__thiscall *)(void *)>(Offsets::FUN_CMAPCHUNK_CTOR)(o);
    Tile &t = *c.tile;
    const uint32_t column = c.index % Offsets::TILE_CHUNKS;
    const uint32_t row = c.index / Offsets::TILE_CHUNKS;
    const uint32_t globalColumn = t.column * Offsets::TILE_CHUNKS + column;
    const uint32_t globalRow = t.row * Offsets::TILE_CHUNKS + row;
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_COLUMN) = column;
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_ROW) = row;
    Game::Ref<uint16_t>(o, Offsets::OFF_CMAPCHUNK_FLAGS) = 0;
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_INDEX) = c.index;
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_GLOBAL_COLUMN) = globalColumn;
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_GLOBAL_ROW) = globalRow;
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_COLUMN8) = globalColumn * 8;
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_ROW8) = globalRow * 8;

    const uint8_t *record = At(t.file, U32(McinEntry(t, c.index)));
    Game::Ref<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_RECORD) = record;
    reinterpret_cast<void(__thiscall *)(void *, const void *)>(Offsets::FUN_CMAPCHUNK_FIXUP_RECORD)(o, record);
    const uint8_t *header = Game::Read<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_HEADER);
    Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_AREA_ID) = U32(header + Offsets::OFF_MCNK_AREA_ID);
    std::memcpy(Game::Ptr<uint8_t>(o, Offsets::OFF_CMAPCHUNK_POSITION), header + Offsets::OFF_MCNK_POSITION, 12);
    Game::Ref<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_LOW_TEXTURES) = header + Offsets::OFF_MCNK_LOW_TEXTURES;
    Game::Ref<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_DOODAD_MAP) = header + Offsets::OFF_MCNK_DOODAD_MAP;
    reinterpret_cast<This_t>(Offsets::FUN_CMAPCHUNK_BUILD_VERTICES)(o);
    reinterpret_cast<This_t>(Offsets::FUN_CMAPCHUNK_LOAD_NORMALS)(o);

    const uint32_t flags = U32(header + Offsets::OFF_MCNK_FLAGS);
    Game::Ref<uint16_t>(o, Offsets::OFF_CMAPCHUNK_FLAGS) =
        (flags & Offsets::MCNK_FLAG_IMPASSABLE) != 0 ? Offsets::CMAPCHUNK_FLAG_IMPASSABLE : 0;
    using AddLayer_t = void(__thiscall *)(void *, void *area, const void *entry, const void *alpha);
    const uint8_t *entry = Game::Read<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_MCLY);
    const uint8_t *alpha = Game::Read<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_MCAL);
    const uint32_t layers = U32(header + Offsets::OFF_MCNK_LAYER_COUNT);
    for (uint32_t i = 0; i < layers; ++i, entry += Offsets::MCLY_ENTRY_SIZE)
        reinterpret_cast<AddLayer_t>(Offsets::FUN_CMAPCHUNK_ADD_LAYER)(o, t.area->bytes, entry, alpha);
    // Load 0x006AF6E5: the shadow needs the record's flag and map shadows.
    if ((flags & Offsets::MCNK_FLAG_SHADOW) != 0 &&
        (Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS) & Offsets::WORLD_FLAG_MAP_SHADOWS) != 0) {
        Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_SHADOW_WORD) = U32(header + Offsets::OFF_MCNK_SHADOW_WORD);
        reinterpret_cast<void(__thiscall *)(void *, const void *)>(Offsets::FUN_CMAPCHUNK_CREATE_SHADOW)(
            o, Game::Read<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_MCSH));
    } else {
        Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_SHADOW_WORD) = 0;
        Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_SHADOW_MAP) = 0;
    }
    if (Game::Read<uint32_t>(Offsets::VAR_TERRAIN_SHADER_FLAG_B) != 0 ||
        Game::Read<uint32_t>(Offsets::VAR_TERRAIN_SHADER_FLAG) != 0)
        reinterpret_cast<This_t>(Offsets::FUN_CMAPCHUNK_CREATE_COMBINED)(o);
    Game::Ref<uint8_t>(o, Offsets::OFF_CMAPCHUNK_FLAGS) |= Offsets::CMAPCHUNK_FLAG_LIGHTS;
    Game::Ref<uint8_t>(o, Offsets::OFF_CMAPCHUNK_FLAGS) |= Offsets::CMAPCHUNK_FLAG_FILED;
}

// LinkNeighbours' rule (0x006C29C0) over the scene's own chunks: a row
// before, a column after, a row after, a column before.
void LinkNeighbours(Land &land) {
    std::map<std::pair<uint32_t, uint32_t>, void *> at;
    for (Chunk &c : land.chunks)
        at[{Game::Read<uint32_t>(c.object(), Offsets::OFF_CMAPCHUNK_GLOBAL_ROW),
            Game::Read<uint32_t>(c.object(), Offsets::OFF_CMAPCHUNK_GLOBAL_COLUMN)}] = c.object();
    const int steps[4][2] = {{-1, 0}, {0, 1}, {1, 0}, {0, -1}};
    for (Chunk &c : land.chunks) {
        void *o = c.object();
        const uint32_t row = Game::Read<uint32_t>(o, Offsets::OFF_CMAPCHUNK_GLOBAL_ROW);
        const uint32_t column = Game::Read<uint32_t>(o, Offsets::OFF_CMAPCHUNK_GLOBAL_COLUMN);
        for (int i = 0; i < 4; ++i) {
            const auto found = at.find({row + steps[i][0], column + steps[i][1]});
            Game::Ref<void *>(o, Offsets::OFF_CMAPCHUNK_NEIGHBOURS + i * 4) =
                found != at.end() ? found->second : nullptr;
        }
    }
}

void AttachBuffers(Land &land) {
    for (size_t i = 0; i < land.chunks.size(); ++i) {
        void *o = land.chunks[i].object();
        Game::Ref<void *>(o, Offsets::OFF_CMAPCHUNK_VERTEX_BUFFER) = land.buffers[i].first;
        Game::Ref<void *>(o, Offsets::OFF_CMAPCHUNK_INDEX_BUFFER) = land.buffers[i].second;
    }
}

void BuildAll(Land &land) {
    land.built = Settings::Current();
    for (auto &t : land.tiles)
        LoadTextures(*t);
    for (Chunk &c : land.chunks)
        BuildChunk(c);
    LinkNeighbours(land);
    AttachBuffers(land);
}

// UnloadChunk and the destructor, as DestroyChunk (0x006A6A00) does less
// the pool: the buffers are the scene's and stay; then each tile's
// textures.
void UnloadAll(Land &land) {
    using This_t = void(__thiscall *)(void *);
    for (Chunk &c : land.chunks) {
        void *o = c.object();
        reinterpret_cast<This_t>(Offsets::FUN_UNLOAD_CHUNK)(o);
        reinterpret_cast<This_t>(Offsets::FUN_CMAPCHUNK_DTOR)(o);
    }
    for (auto &t : land.tiles)
        CloseTextures(*t);
}

// The scene's pool, made as CreateChunkBufferPool makes the world's: one
// vertex and one index pool, a full-level pair of sub-buffers per chunk.
bool MakeBuffers(Land &land) {
    using Pool_t = void *(__fastcall *)(uint32_t kind, uint32_t usage, uint32_t size, uint32_t arg, const char *name);
    using Buffer_t = void *(__fastcall *)(void *pool, uint32_t itemSize, uint32_t count, uint32_t offset);
    const uint32_t n = static_cast<uint32_t>(land.chunks.size());
    const uint32_t vertexBytes = FullVertexCount() * Offsets::CHUNK_VERTEX_SIZE;
    const uint32_t indexBytes = FullIndexCount() * Offsets::CHUNK_INDEX_SIZE;
    land.vertexPool = reinterpret_cast<Pool_t>(Offsets::FUN_GX_CREATE_POOL)(
        Offsets::GX_POOL_KIND_VERTEX, Offsets::GX_POOL_USAGE_STATIC, n * vertexBytes, Offsets::GX_POOL_ARG,
        kVertexPoolName);
    land.indexPool = reinterpret_cast<Pool_t>(Offsets::FUN_GX_CREATE_POOL)(
        Offsets::GX_POOL_KIND_INDEX, Offsets::GX_POOL_USAGE_STATIC, n * indexBytes, Offsets::GX_POOL_ARG,
        kIndexPoolName);
    if (land.vertexPool == nullptr || land.indexPool == nullptr)
        return false;
    for (uint32_t i = 0; i < n; ++i) {
        void *v = reinterpret_cast<Buffer_t>(Offsets::FUN_GX_POOL_BUFFER)(
            land.vertexPool, Offsets::CHUNK_VERTEX_SIZE, FullVertexCount(), i * vertexBytes);
        void *x = reinterpret_cast<Buffer_t>(Offsets::FUN_GX_POOL_BUFFER)(
            land.indexPool, Offsets::CHUNK_INDEX_SIZE, FullIndexCount(), i * indexBytes);
        land.buffers.emplace_back(v, x);
        if (v == nullptr || x == nullptr)
            return false;
    }
    return true;
}

// FreeChunkBufferPool's order (0x006AE5C0): each sub-buffer, then the pools.
void FreeBuffers(Land &land) {
    using Free_t = void(__fastcall *)(void *buffer);
    for (auto &pair : land.buffers) {
        if (pair.first != nullptr)
            reinterpret_cast<Free_t>(Offsets::FUN_GX_FREE_BUFFER)(pair.first);
        if (pair.second != nullptr)
            reinterpret_cast<Free_t>(Offsets::FUN_GX_FREE_BUFFER)(pair.second);
    }
    land.buffers.clear();
    for (void *pool : {land.indexPool, land.vertexPool})
        if (pool != nullptr)
            reinterpret_cast<Free_t>(Offsets::FUN_GX_BUFFER_RELEASE)(pool);
    land.vertexPool = land.indexPool = nullptr;
}

// The distance from a point to a chunk's square, its corner at the MCNK
// position and its sides one 16th of a tile toward the map's far side.
bool ChunkInRadius(const uint8_t *record, float x, float y, float radius, float side) {
    const uint8_t *position = record + Offsets::MCNK_RECORD_HEADER + Offsets::OFF_MCNK_POSITION;
    const float maxX = F32(position), maxY = F32(position + 4);
    const float dx = x < maxX - side ? maxX - side - x : (x > maxX ? x - maxX : 0.0f);
    const float dy = y < maxY - side ? maxY - side - y : (y > maxY ? y - maxY : 0.0f);
    return dx * dx + dy * dy <= radius * radius;
}

int TileIndex(float origin, float coordinate, float tile) {
    return static_cast<int>(std::floor((origin - coordinate) / tile));
}

} // namespace

Land *Load(uint32_t mapID, float x, float y, float radius) {
    if (!(radius >= 0.0f) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius))
        return nullptr;
    const char *dir =
        DBC::StringField(Offsets::VAR_MAP_RECORDS, Offsets::VAR_MAP_COUNT, mapID, Offsets::OFF_MAP_DIRECTORY);
    if (dir == nullptr || *dir == '\0')
        return nullptr;
    const float origin = Game::Read<float>(Offsets::VAR_MAP_ORIGIN);
    const float tile = Game::Read<float>(Offsets::VAR_TILE_SIZE);
    const float side = tile / static_cast<float>(Offsets::TILE_CHUNKS);
    const int last = Offsets::MAP_TILES - 1;
    const auto clamp = [last](int v) { return v < 0 ? 0 : (v > last ? last : v); };
    const int rowFirst = clamp(TileIndex(origin, x + radius, tile));
    const int rowLast = clamp(TileIndex(origin, x - radius, tile));
    const int columnFirst = clamp(TileIndex(origin, y + radius, tile));
    const int columnLast = clamp(TileIndex(origin, y - radius, tile));
    auto land = std::make_unique<Land>();
    for (int row = rowFirst; row <= rowLast; ++row) {
        for (int column = columnFirst; column <= columnLast; ++column) {
            char path[260];
            std::snprintf(path, sizeof path, "World\\Maps\\%s\\%s_%d_%d.adt", dir, dir, column, row);
            auto t = std::make_unique<Tile>();
            t->column = static_cast<uint32_t>(column);
            t->row = static_cast<uint32_t>(row);
            if (!ReadFile(path, t->file) || !ParseHeader(*t))
                continue;
            const uint32_t textures = TextureCount(*t);
            for (uint32_t i = 0; i < Offsets::ADT_CHUNKS; ++i) {
                const uint8_t *record = CheckedRecord(*t, i, textures);
                if (record != nullptr && ChunkInRadius(record, x, y, radius, side))
                    t->wanted.push_back(i);
            }
            if (t->wanted.empty())
                continue;
            for (uint32_t i : t->wanted) {
                Chunk c;
                c.memory = std::make_unique<ChunkMemory>();
                c.tile = t.get();
                c.index = i;
                land->chunks.push_back(std::move(c));
            }
            land->tiles.push_back(std::move(t));
        }
    }
    if (land->chunks.empty())
        return nullptr;
    if (!MakeBuffers(*land)) {
        FreeBuffers(*land);
        return nullptr;
    }
    BuildAll(*land);
    return land.release();
}

void Free(Land *land) {
    if (land == nullptr)
        return;
    UnloadAll(*land);
    FreeBuffers(*land);
    delete land;
}

void Draw(Land &land, void *frame, const float (&eye)[3], float farClip) {
    const uint32_t worldFlags = Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS);
    if ((worldFlags & Offsets::WORLD_FLAG_CHUNKS) == 0)
        return;
    if (!(Settings::Current() == land.built)) {
        UnloadAll(land);
        BuildAll(land);
    }

    // The fog end the world's draw compares with: the scene's fog, else the
    // far clip, past which nothing draws.
    const bool fogged =
        (Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) & Offsets::SIMPLEMODEL_FOG_ARMED) != 0;
    const float fogEnd = fogged ? Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) : farClip;

    // The chunk update (0x006AFAD0) for every chunk: the sphere's view depth
    // and the level, before any mesh is keyed (SelectMesh reads the
    // neighbours' levels). The view looks from the origin, so its third
    // column is the view axis for a point relative to the eye.
    Matrix view;
    Gx::GetView(view);
    const bool lod = (worldFlags & Offsets::WORLD_FLAG_LOD) != 0;
    const float lodDist = Game::Read<float>(Offsets::VAR_CHUNK_LOD_DIST);
    const uint32_t nearLevel = Game::Read<uint32_t>(Offsets::VAR_CHUNK_MAX_LOD);
    const uint32_t farLevel = Game::Read<uint32_t>(Offsets::VAR_CHUNK_FAR_LOD);
    for (Chunk &c : land.chunks) {
        void *o = c.object();
        const float *centre = Game::Ptr<float>(o, Offsets::OFF_CMAPCHUNK_CENTRE);
        const float depth = (centre[0] - eye[0]) * view[2] + (centre[1] - eye[1]) * view[6] +
                            (centre[2] - eye[2]) * view[10] - Game::Read<float>(o, Offsets::OFF_CMAPCHUNK_RADIUS);
        Game::Ref<float>(o, Offsets::OFF_CMAPCHUNK_VIEW_DEPTH) = depth;
        Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_LEVEL) = lod && depth > lodDist ? farLevel : nearLevel;
    }

    alignas(16) uint8_t volume[Offsets::FRUSTUM_VOLUME_SIZE] = {};
    World::ViewVolume(volume, eye);
    float &cameraX = Game::Ref<float>(Offsets::VAR_CAMERA_POSITION);
    float *camera = &cameraX;
    float &worldFogEnd = Game::Ref<float>(Offsets::VAR_FOG_END);
    const float savedCamera[3] = {camera[0], camera[1], camera[2]};
    const float savedFogEnd = worldFogEnd;
    camera[0] = eye[0];
    camera[1] = eye[1];
    camera[2] = eye[2];
    worldFogEnd = fogEnd;

    using Fn_t = void(__cdecl *)();
    const Gx::SavedStates states;
    if ((worldFlags & Offsets::WORLD_FLAG_ANISOTROPIC) != 0) {
        const float bias = Game::Read<float>(Offsets::VAR_CHUNK_MIP_BIAS_IMMEDIATE);
        for (int i = 0; i < Offsets::MIP_BIAS_STAGES; ++i)
            Gx::SetStateFloat(Offsets::GXRS_MIP_BIAS_STAGE0 + i, bias);
    }
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_PUSH)();
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_FRUSTUM_STORE)(volume);
    Gx::PushTransform(kWorldStack);
    void *scene = Model::Pane::Scene(frame);
    using Culled_t = uint32_t(__thiscall *)(const float *box);
    using Init_t = void(__thiscall *)(void *record, void *unused, const float *sphere);
    using Lights_t = void(__thiscall *)(void *scene, void *record);
    using Commit_t = void(__thiscall *)(void *record, const float *origin, int fog);
    using Callback_t = void(__fastcall *)(void *instance, void *record, void *frame);
    using Mesh_t = void(__thiscall *)(void *chunk, uint32_t draw);
    for (Chunk &c : land.chunks) {
        void *o = c.object();
        const uint8_t *header = Game::Read<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_HEADER);
        if (Game::Read<uint16_t>(header, Offsets::OFF_MCNK_HOLES) == Offsets::MCNK_HOLES_ALL ||
            reinterpret_cast<Culled_t>(Offsets::FUN_FRUSTUM_IS_BOX_CULLED)(
                Game::Ptr<float>(o, Offsets::OFF_CMAPCHUNK_BOX)) != 0)
            continue;
        const float *position = Game::Ptr<float>(o, Offsets::OFF_CMAPCHUNK_POSITION);
        Matrix world;
        Identity(world);
        Translate(world, {position[0] - eye[0], position[1] - eye[1], position[2] - eye[2]});
        Gx::SetTransform(kWorldStack, world);

        alignas(16) uint8_t record[Offsets::M2_LIGHTING_RECORD_SIZE] = {};
        reinterpret_cast<Init_t>(Offsets::FUN_M2LIGHTING_INIT)(record, nullptr,
                                                              Game::Ptr<float>(o, Offsets::OFF_CMAPCHUNK_CENTRE));
        reinterpret_cast<Lights_t>(Offsets::FUN_M2SCENE_ADD_LIGHTS_FOR_POINT)(scene, record);
        // The chunk's slots: the fog always, the light only nearer than the
        // fog end by twice the radius (a NaN depth adds none).
        const float reach = Game::Read<float>(o, Offsets::OFF_CMAPCHUNK_VIEW_DEPTH) -
                            Game::Read<float>(o, Offsets::OFF_CMAPCHUNK_RADIUS);
        if (reach < fogEnd)
            reinterpret_cast<Callback_t>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB)(nullptr, record, frame);
        else
            World::FrameFog(record, frame);
        reinterpret_cast<Commit_t>(Offsets::FUN_M2LIGHTING_SET_DEVICE_LIGHTS)(record, eye, 1);
        reinterpret_cast<Mesh_t>(Offsets::FUN_CMAPCHUNK_SELECT_MESH)(o, 1);
    }
    Gx::PopTransform(kWorldStack);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_POP)();
    camera[0] = savedCamera[0];
    camera[1] = savedCamera[1];
    camera[2] = savedCamera[2];
    worldFogEnd = savedFogEnd;
}

} // namespace Model::Scene::Terrain
