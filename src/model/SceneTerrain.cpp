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
#include "model/SceneContact.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <memory>
#include <utility>
#include <vector>

namespace Model::Scene::Terrain {

namespace {

using Gx::Identity;
using Gx::Matrix;
using Gx::Translate;

constexpr char kVertexPoolName[] = "ModelScene_terrain_vtx";
constexpr char kIndexPoolName[] = "ModelScene_terrain_idx";
constexpr char kLiquidVertexPoolName[] = "ModelScene_liquid_vtx";
constexpr char kLiquidIndexPoolName[] = "ModelScene_liquid_idx";

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
    // The model tables (MMDX names, MMID offsets, MDDF placements), or null.
    const char *modelNames = nullptr;
    uint32_t modelNamesSize = 0;
    const uint32_t *modelOffsets = nullptr;
    uint32_t modelCount = 0;
    const uint8_t *placements = nullptr;
    uint32_t placementCount = 0;
};

struct Chunk {
    std::unique_ptr<ChunkMemory> memory;
    Tile *tile = nullptr;
    uint32_t index = 0; // row * 16 + column in the tile
    std::vector<uint32_t> props; // its MCRF doodads, as indices into the land's props
    // CreateDetailDoodads reads 512 bytes at the MCSH pointer whether or not
    // the chunk has a shadow map; false when they lie outside the file.
    bool detailSafe = false;
    void *object() { return memory->bytes; }
};

} // namespace

struct Land {
    std::vector<std::unique_ptr<Tile>> tiles;
    std::vector<Chunk> chunks;
    void *vertexPool = nullptr;
    void *indexPool = nullptr;
    std::vector<std::pair<void *, void *>> buffers; // one pair per chunk, in order
    // The liquid slots' buffers, one pair per slot in chunk and slot order.
    void *liquidVertexPool = nullptr;
    void *liquidIndexPool = nullptr;
    std::vector<std::pair<void *, void *>> liquidBuffers;
    uint32_t mapID = 0;
    Settings built;
    // The chunks by (global row, global column), as the world finds them in
    // its area table.
    std::map<std::pair<uint32_t, uint32_t>, void *> byCell;
    // Each chunk object's props, for the box query.
    std::map<const void *, const std::vector<uint32_t> *> propsOf;
    World::Props *props = nullptr;
    uint32_t serial = 0; // this terrain among all loaded, for the actors' caches
    uint32_t detailStamp = 0; // the detail pools' rebuild stamp the batches were made under
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
    // The model tables (0x006C2010): each offset points 8 bytes before its
    // data, whose size is the dword before it. A tile whose tables do not
    // fit keeps its ground and has no doodads.
    const auto table = [&](uint32_t field, const uint8_t *&data, uint32_t &bytes) {
        const uint64_t at = mhdr + U32(At(f, mhdr + field)) + Offsets::MCNK_RECORD_HEADER;
        if (U32(At(f, mhdr + field)) == 0 || at < 4 || !Inside(f, at, 0) || !Inside(f, at, U32(At(f, at - 4))))
            return false;
        data = At(f, at);
        bytes = U32(At(f, at - 4));
        return true;
    };
    const uint8_t *names = nullptr, *offsets = nullptr, *placements = nullptr;
    uint32_t namesBytes = 0, offsetBytes = 0, placementBytes = 0;
    if (Inside(f, mhdr, Offsets::OFF_MHDR_MDDF + 4) && table(Offsets::OFF_MHDR_MMDX, names, namesBytes) &&
        table(Offsets::OFF_MHDR_MMID, offsets, offsetBytes) &&
        table(Offsets::OFF_MHDR_MDDF, placements, placementBytes)) {
        t.modelNames = reinterpret_cast<const char *>(names);
        t.modelNamesSize = namesBytes;
        t.modelOffsets = reinterpret_cast<const uint32_t *>(offsets);
        t.modelCount = offsetBytes / sizeof(uint32_t);
        t.placements = placements;
        t.placementCount = placementBytes / Offsets::MDDF_SIZE;
    }
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
    // LoadLiquids reads one MCLQ block per liquid flag, back to back.
    uint32_t liquids = 0;
    for (uint32_t i = 0; i < Offsets::CHUNK_LIQUID_SLOTS; ++i)
        if ((U32(header + Offsets::OFF_MCNK_FLAGS) & (Offsets::MCNK_FLAG_LIQUID_FIRST << i)) != 0)
            ++liquids;
    if (liquids != 0 && !sub(Offsets::OFF_MCNK_MCLQ, uint64_t{liquids} * Offsets::MCLQ_SIZE))
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
    reinterpret_cast<void(__thiscall *)(void *, uint32_t)>(Offsets::FUN_CMAPCHUNK_LOAD_LIQUIDS)(o, 1);

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
    std::map<std::pair<uint32_t, uint32_t>, void *> &at = land.byCell;
    at.clear();
    land.propsOf.clear();
    for (Chunk &c : land.chunks) {
        at[{Game::Read<uint32_t>(c.object(), Offsets::OFF_CMAPCHUNK_GLOBAL_ROW),
            Game::Read<uint32_t>(c.object(), Offsets::OFF_CMAPCHUNK_GLOBAL_COLUMN)}] = c.object();
        land.propsOf[c.object()] = &c.props;
    }
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

// LoadReferences' doodad leg (0x006B00F0) for the scene's chunks: each MCRF
// doodad index into MDDF, one prop per unique id (the def hash 0x00CA7DDC).
// An index or a name outside the tile's tables is skipped.
void MakeProps(Land &land, void *frame) {
    land.props = World::NewProps();
    std::unordered_map<uint32_t, uint32_t> byUniqueId;
    for (Chunk &c : land.chunks) {
        const Tile &t = *c.tile;
        if (t.placements == nullptr)
            continue;
        const uint8_t *record = At(t.file, U32(McinEntry(t, c.index)));
        const uint8_t *header = record + Offsets::MCNK_RECORD_HEADER;
        const uint32_t refs = U32(header + Offsets::OFF_MCNK_DOODAD_REF_COUNT);
        const uint64_t list = uint64_t{U32(McinEntry(t, c.index))} + U32(header + Offsets::OFF_MCNK_MCRF) +
                              Offsets::MCNK_RECORD_HEADER;
        if (!Inside(t.file, list, uint64_t{refs} * sizeof(uint32_t)))
            continue;
        for (uint32_t i = 0; i < refs; ++i) {
            const uint32_t placement = U32(At(t.file, list + uint64_t{i} * sizeof(uint32_t)));
            if (placement >= t.placementCount)
                continue;
            const uint8_t *entry = t.placements + static_cast<size_t>(placement) * Offsets::MDDF_SIZE;
            const uint32_t nameIndex = U32(entry + Offsets::OFF_MDDF_NAME_INDEX);
            if (nameIndex >= t.modelCount)
                continue;
            const uint32_t nameOffset = t.modelOffsets[nameIndex];
            if (nameOffset >= t.modelNamesSize ||
                std::memchr(t.modelNames + nameOffset, 0, t.modelNamesSize - nameOffset) == nullptr)
                continue;
            const uint32_t unique = U32(entry + Offsets::OFF_MDDF_UNIQUE_ID);
            auto found = byUniqueId.find(unique);
            if (found == byUniqueId.end())
                found = byUniqueId
                            .emplace(unique, World::AddProp(*land.props, frame, t.modelNames + nameOffset, entry))
                            .first;
            c.props.push_back(found->second);
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

void *LiquidSlot(void *chunk, uint32_t slot) {
    return Game::Read<void *>(chunk, Offsets::OFF_CMAPCHUNK_LIQUIDS + slot * sizeof(void *));
}

// The scene's liquid buffers onto its slots, in chunk and slot order, marked
// unfilled (a rebuilt slot fills them again).
void AttachLiquidBuffers(Land &land) {
    using Invalidate_t = void(__fastcall *)(void *buffer);
    size_t next = 0;
    for (Chunk &c : land.chunks) {
        for (uint32_t i = 0; i < Offsets::CHUNK_LIQUID_SLOTS; ++i) {
            void *slot = LiquidSlot(c.object(), i);
            if (slot == nullptr || next >= land.liquidBuffers.size())
                continue;
            const auto &pair = land.liquidBuffers[next++];
            Game::Ref<void *>(slot, Offsets::OFF_CHUNKLIQUID_VERTEX_BUFFER) = pair.first;
            Game::Ref<void *>(slot, Offsets::OFF_CHUNKLIQUID_INDEX_BUFFER) = pair.second;
            reinterpret_cast<Invalidate_t>(Offsets::FUN_GX_BUFFER_INVALIDATE)(pair.first);
            reinterpret_cast<Invalidate_t>(Offsets::FUN_GX_BUFFER_INVALIDATE)(pair.second);
        }
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
    AttachLiquidBuffers(land);
}

// The detail doodad batch of a chunk, freed (FreeDetailDoodadBatch); it holds
// no buffers between draws.
void FreeDetail(void *chunk) {
    void *&batch = Game::Ref<void *>(chunk, Offsets::OFF_CMAPCHUNK_DETAIL_BATCH);
    if (batch == nullptr)
        return;
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_FREE_DETAIL_DOODAD_BATCH)(batch);
    batch = nullptr;
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

// The liquid pools, made as CreateLiquidBufferPool (0x0068CA50) makes the
// world's: a vertex and an index pair for each of the slots.
void MakeLiquidBuffers(Land &land) {
    using Pool_t = void *(__fastcall *)(uint32_t kind, uint32_t usage, uint32_t size, uint32_t arg, const char *name);
    using Buffer_t = void *(__fastcall *)(void *pool, uint32_t itemSize, uint32_t count, uint32_t offset);
    uint32_t slots = 0;
    for (Chunk &c : land.chunks)
        for (uint32_t i = 0; i < Offsets::CHUNK_LIQUID_SLOTS; ++i)
            if (LiquidSlot(c.object(), i) != nullptr)
                ++slots;
    if (slots == 0)
        return;
    const uint32_t vertexBytes = Offsets::LIQUID_VERTEX_SIZE * Offsets::LIQUID_VERTEX_COUNT;
    const uint32_t indexBytes = Offsets::CHUNK_INDEX_SIZE * Offsets::LIQUID_INDEX_COUNT;
    land.liquidVertexPool = reinterpret_cast<Pool_t>(Offsets::FUN_GX_CREATE_POOL)(
        Offsets::GX_POOL_KIND_VERTEX, Offsets::GX_POOL_USAGE_STATIC, slots * vertexBytes, Offsets::GX_POOL_ARG,
        kLiquidVertexPoolName);
    land.liquidIndexPool = reinterpret_cast<Pool_t>(Offsets::FUN_GX_CREATE_POOL)(
        Offsets::GX_POOL_KIND_INDEX, Offsets::GX_POOL_USAGE_STATIC, slots * indexBytes, Offsets::GX_POOL_ARG,
        kLiquidIndexPoolName);
    if (land.liquidVertexPool == nullptr || land.liquidIndexPool == nullptr)
        return;
    for (uint32_t i = 0; i < slots; ++i) {
        void *v = reinterpret_cast<Buffer_t>(Offsets::FUN_GX_POOL_BUFFER)(
            land.liquidVertexPool, Offsets::LIQUID_VERTEX_SIZE, Offsets::LIQUID_VERTEX_COUNT, i * vertexBytes);
        void *x = reinterpret_cast<Buffer_t>(Offsets::FUN_GX_POOL_BUFFER)(
            land.liquidIndexPool, Offsets::CHUNK_INDEX_SIZE, Offsets::LIQUID_INDEX_COUNT, i * indexBytes);
        if (v == nullptr || x == nullptr)
            break;
        land.liquidBuffers.emplace_back(v, x);
    }
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
    for (auto &pair : land.liquidBuffers) {
        reinterpret_cast<Free_t>(Offsets::FUN_GX_FREE_BUFFER)(pair.first);
        reinterpret_cast<Free_t>(Offsets::FUN_GX_FREE_BUFFER)(pair.second);
    }
    land.liquidBuffers.clear();
    for (void *pool : {land.indexPool, land.vertexPool, land.liquidIndexPool, land.liquidVertexPool})
        if (pool != nullptr)
            reinterpret_cast<Free_t>(Offsets::FUN_GX_BUFFER_RELEASE)(pool);
    land.vertexPool = land.indexPool = land.liquidVertexPool = land.liquidIndexPool = nullptr;
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

Land *Load(void *frame, uint32_t mapID, float x, float y, float radius) {
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
                const uint32_t record = U32(McinEntry(*t, i));
                const uint8_t *header = At(t->file, uint64_t{record} + Offsets::MCNK_RECORD_HEADER);
                c.detailSafe = Inside(t->file,
                                      uint64_t{record} + U32(header + Offsets::OFF_MCNK_MCSH) +
                                          Offsets::MCNK_RECORD_HEADER,
                                      Offsets::MCSH_SIZE);
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
    land->mapID = mapID;
    BuildAll(*land);
    MakeLiquidBuffers(*land);
    AttachLiquidBuffers(*land);
    MakeProps(*land, frame);
    static uint32_t serials = 0;
    land->serial = ++serials;
    return land.release();
}

uint32_t Serial(const Land &land) { return land.serial; }

uint32_t MapOf(const Land &land) { return land.mapID; }

uint32_t AreaAt(const Land &land, const float (&point)[3]) {
    // 0x0069B126: x's distance stored, then times k and stored; y's product
    // stored once; each less a half, rounded.
    const double origin = Game::Read<float>(Offsets::VAR_MAP_ORIGIN);
    const double perYard = Game::Read<float>(Offsets::VAR_AREA_CHUNKS_PER_YARD);
    const double half = Game::Read<float>(Offsets::VAR_POINT_HALF_CELL);
    const float fromX = static_cast<float>(-(static_cast<double>(point[0]) - origin));
    const float y = static_cast<float>(-(static_cast<double>(point[1]) - origin) * perYard);
    const float x = static_cast<float>(perYard * fromX);
    const int32_t column = static_cast<int32_t>(std::lrint(static_cast<double>(y) - half));
    const int32_t row = static_cast<int32_t>(std::lrint(static_cast<double>(x) - half));
    if (row < 0 || column < 0)
        return 0;
    const auto found = land.byCell.find({static_cast<uint32_t>(row), static_cast<uint32_t>(column)});
    return found != land.byCell.end() ? Game::Read<uint32_t>(found->second, Offsets::OFF_CMAPCHUNK_AREA_ID) : 0;
}

void Update(Land &land, void *frame, bool sounds) {
    if (land.props != nullptr)
        World::UpdateProps(*land.props, frame, sounds);
}

void Silence(Land &land) {
    if (land.props != nullptr)
        World::SilenceProps(*land.props);
}

void Free(Land *land) {
    if (land == nullptr)
        return;
    World::FreeProps(land->props);
    UnloadAll(*land);
    FreeBuffers(*land);
    delete land;
}

namespace {

// What a terrain pass draws under, as the world's frame has it: the camera and
// the fog end the chunk and liquid draws read (the scene's while it draws), the
// scene's volume on the frustum stack and the world transform stack pushed.
class Staged {
  public:
    Staged(void *frame, const float (&eye)[3], float farClip) : eye_(eye) {
        // The fog end the world's draws compare with: the scene's fog, else
        // the far clip, past which nothing draws.
        const bool fogged =
            (Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) & Offsets::SIMPLEMODEL_FOG_ARMED) != 0;
        fogEnd = fogged ? Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) : farClip;
        // The view looks from the origin, so its third column is the view
        // axis for a point relative to the eye.
        Gx::GetView(view);
        alignas(16) uint8_t volume[Offsets::FRUSTUM_VOLUME_SIZE] = {};
        World::ViewVolume(volume, eye);
        float *camera = &Game::Ref<float>(Offsets::VAR_CAMERA_POSITION);
        float &worldFogEnd = Game::Ref<float>(Offsets::VAR_FOG_END);
        for (int i = 0; i < 3; ++i) {
            savedCamera_[i] = camera[i];
            camera[i] = eye[i];
        }
        savedFogEnd_ = worldFogEnd;
        worldFogEnd = fogEnd;
        reinterpret_cast<void(__cdecl *)()>(Offsets::FUN_FRUSTUM_PUSH)();
        reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_FRUSTUM_STORE)(volume);
        Gx::PushTransform(Offsets::GX_XFORM_WORLD);
    }
    ~Staged() {
        Gx::PopTransform(Offsets::GX_XFORM_WORLD);
        reinterpret_cast<void(__cdecl *)()>(Offsets::FUN_FRUSTUM_POP)();
        float *camera = &Game::Ref<float>(Offsets::VAR_CAMERA_POSITION);
        for (int i = 0; i < 3; ++i)
            camera[i] = savedCamera_[i];
        Game::Ref<float>(Offsets::VAR_FOG_END) = savedFogEnd_;
    }
    Staged(const Staged &) = delete;
    Staged &operator=(const Staged &) = delete;

    // The depth along the view axis of a point, less `radius`.
    float Depth(const float *p, float radius) const {
        return (p[0] - eye_[0]) * view[2] + (p[1] - eye_[1]) * view[6] + (p[2] - eye_[2]) * view[10] - radius;
    }
    // The world transform at a chunk's position less the eye.
    void PlaceAt(const void *chunk) const {
        const float *position = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_POSITION);
        Matrix world;
        Identity(world);
        Translate(world, {position[0] - eye_[0], position[1] - eye_[1], position[2] - eye_[2]});
        Gx::SetTransform(Offsets::GX_XFORM_WORLD, world);
    }
    bool Culled(const float (&box)[6]) const {
        return reinterpret_cast<uint32_t(__thiscall *)(const float *)>(Offsets::FUN_FRUSTUM_IS_BOX_CULLED)(box) != 0;
    }

    float fogEnd = 0.0f;
    Matrix view;

  private:
    const float (&eye_)[3];
    float savedCamera_[3] = {};
    float savedFogEnd_ = 0.0f;
};

// A lighting record committed at the eye: the scene's lights at the sphere,
// the scene's fog, and its light (the sun's stand-in) when `lit`.
void CommitRecord(void *frame, const float (&sphere)[4], bool lit, const float (&eye)[3]) {
    using Init_t = void(__thiscall *)(void *record, void *unused, const float *sphere);
    using Lights_t = void(__thiscall *)(void *scene, void *record);
    using Commit_t = void(__thiscall *)(void *record, const float *origin, int fog);
    using Callback_t = void(__fastcall *)(void *instance, void *record, void *frame);
    alignas(16) uint8_t record[Offsets::M2_LIGHTING_RECORD_SIZE] = {};
    reinterpret_cast<Init_t>(Offsets::FUN_M2LIGHTING_INIT)(record, nullptr, sphere);
    reinterpret_cast<Lights_t>(Offsets::FUN_M2SCENE_ADD_LIGHTS_FOR_POINT)(Model::Pane::Scene(frame), record);
    if (lit)
        reinterpret_cast<Callback_t>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB)(nullptr, record, frame);
    else
        World::FrameFog(record, frame);
    reinterpret_cast<Commit_t>(Offsets::FUN_M2LIGHTING_SET_DEVICE_LIGHTS)(record, eye, 1);
}

} // namespace

void Draw(Land &land, void *frame, const float (&eye)[3], float farClip) {
    const uint32_t worldFlags = Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS);
    if ((worldFlags & Offsets::WORLD_FLAG_CHUNKS) == 0)
        return;
    if (!(Settings::Current() == land.built)) {
        UnloadAll(land);
        BuildAll(land);
    }
    // The detail pools, as the world's frame prepares them (0x0068121D) before
    // its passes; when they were rebuilt since, the scene's batches were made
    // for the old density and are scattered again.
    if (Game::Read<uint32_t>(Offsets::VAR_DETAIL_DOODAD_REBUILD) != 0)
        reinterpret_cast<void(__cdecl *)()>(Offsets::FUN_PREPARE_DETAIL_DOODAD_BUFFERS)();
    const uint32_t detailStamp = Game::Read<uint32_t>(Offsets::VAR_DETAIL_DOODAD_STAMP);
    if (detailStamp != land.detailStamp) {
        for (Chunk &c : land.chunks)
            FreeDetail(c.object());
        land.detailStamp = detailStamp;
    }
    const bool details = (worldFlags & Offsets::WORLD_FLAG_DETAIL_DOODADS) != 0 &&
                         Game::Read<void *>(Offsets::VAR_WORLD_MODEL_MANAGER) != nullptr;
    const float horizon = Game::Read<float>(Offsets::VAR_DETAIL_DOODAD_HORIZON);
    std::vector<void *> detailed; // the chunks the detail pass draws

    const Staged staged(frame, eye, farClip);
    const float fogEnd = staged.fogEnd;
    // The chunk update (0x006AFAD0) for every chunk: the sphere's view depth
    // and the level, before any mesh is keyed (SelectMesh reads the
    // neighbours' levels).
    const bool lod = (worldFlags & Offsets::WORLD_FLAG_LOD) != 0;
    const float lodDist = Game::Read<float>(Offsets::VAR_CHUNK_LOD_DIST);
    const uint32_t nearLevel = Game::Read<uint32_t>(Offsets::VAR_CHUNK_MAX_LOD);
    const uint32_t farLevel = Game::Read<uint32_t>(Offsets::VAR_CHUNK_FAR_LOD);
    for (Chunk &c : land.chunks) {
        void *o = c.object();
        const float depth = staged.Depth(Game::Ptr<float>(o, Offsets::OFF_CMAPCHUNK_CENTRE),
                                         Game::Read<float>(o, Offsets::OFF_CMAPCHUNK_RADIUS));
        Game::Ref<float>(o, Offsets::OFF_CMAPCHUNK_VIEW_DEPTH) = depth;
        Game::Ref<uint32_t>(o, Offsets::OFF_CMAPCHUNK_LEVEL) = lod && depth > lodDist ? farLevel : nearLevel;
    }

    using Mesh_t = void(__thiscall *)(void *chunk, uint32_t draw);
    // The world transform at the chunk less the eye, and the chunk's lighting
    // record at its sphere: the fog always, the light only nearer than the
    // fog end by twice the radius (a NaN depth adds none).
    const auto placeAndLight = [&](void *o) {
        staged.PlaceAt(o);
        const float *centre = Game::Ptr<float>(o, Offsets::OFF_CMAPCHUNK_CENTRE);
        const float sphere[4] = {centre[0], centre[1], centre[2], Game::Read<float>(o, Offsets::OFF_CMAPCHUNK_RADIUS)};
        const float reach = Game::Read<float>(o, Offsets::OFF_CMAPCHUNK_VIEW_DEPTH) - sphere[3];
        CommitRecord(frame, sphere, reach < fogEnd, eye);
    };
    // The chunk pass in its own state push (DrawChunkPass 0x00684510): the
    // shader path's layer draw (0x006BF690) leaves its pixel program bound,
    // and only the pass's pop takes it off.
    Gx::PushStates();
    if ((worldFlags & Offsets::WORLD_FLAG_ANISOTROPIC) != 0) {
        const float bias = Game::Read<float>(Offsets::VAR_CHUNK_MIP_BIAS_IMMEDIATE);
        for (int i = 0; i < Offsets::MIP_BIAS_STAGES; ++i)
            Gx::SetStateFloat(Offsets::GXRS_MIP_BIAS_STAGE0 + i, bias);
    }
    for (Chunk &c : land.chunks) {
        void *o = c.object();
        const uint8_t *header = Game::Read<const uint8_t *>(o, Offsets::OFF_CMAPCHUNK_HEADER);
        if (Game::Read<uint16_t>(header, Offsets::OFF_MCNK_HOLES) == Offsets::MCNK_HOLES_ALL ||
            staged.Culled(*reinterpret_cast<const float(*)[6]>(Game::Ptr<float>(o, Offsets::OFF_CMAPCHUNK_BOX))))
            continue;
        placeAndLight(o);
        reinterpret_cast<Mesh_t>(Offsets::FUN_CMAPCHUNK_SELECT_MESH)(o, 1);
        // The detail leg (0x006847C9): a chunk inside the horizon scatters its
        // batch once and stays for the detail pass.
        if (details && c.detailSafe && Game::Read<float>(o, Offsets::OFF_CMAPCHUNK_VIEW_DEPTH) < horizon) {
            if (Game::Read<void *>(o, Offsets::OFF_CMAPCHUNK_DETAIL_BATCH) == nullptr)
                reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_CMAPCHUNK_CREATE_DETAIL_DOODADS)(o);
            if (Game::Read<void *>(o, Offsets::OFF_CMAPCHUNK_DETAIL_BATCH) != nullptr)
                detailed.push_back(o);
        }
    }
    Gx::PopStates();
    // The detail pass (0x006813D0): stage 0's mip bias, then each listed
    // chunk's batch at its transform and light. Each batch then gives its
    // slot buffers back (the world's idle release), so none outlives a
    // rebuild of the world's pools.
    if (!detailed.empty()) {
        Gx::PushStates();
        Gx::SetStateFloat(Offsets::GXRS_MIP_BIAS_STAGE0, Game::Read<float>(Offsets::VAR_DETAIL_MIP_BIAS_IMMEDIATE));
        using DrawDetail_t = void(__thiscall *)(void *chunk, float draw);
        for (void *o : detailed) {
            placeAndLight(o);
            reinterpret_cast<DrawDetail_t>(Offsets::FUN_CMAPCHUNK_DRAW_DETAIL_DOODADS)(
                o, std::numeric_limits<float>::denorm_min());
        }
        Gx::PopStates();
        for (void *o : detailed)
            reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_RELEASE_DETAIL_BATCH_BUFFERS)(
                Game::Read<void *>(o, Offsets::OFF_CMAPCHUNK_DETAIL_BATCH));
    }
}

namespace {

// The slots of one pass the scene's view shows (the layer cull 0x00683AB0):
// a slot's box is its chunk's with the slot's heights as z (0x0068DF40).
std::vector<void *> VisibleSlots(Land &land, const Staged &staged, uint32_t pass) {
    std::vector<void *> out;
    for (Chunk &c : land.chunks) {
        void *slot = LiquidSlot(c.object(), pass);
        if (slot == nullptr)
            continue;
        float box[6];
        std::memcpy(box, Game::Ptr<float>(c.object(), Offsets::OFF_CMAPCHUNK_BOX_MIN), sizeof box);
        box[2] = Game::Read<float>(slot, Offsets::OFF_CHUNKLIQUID_LOW);
        box[5] = Game::Read<float>(slot, Offsets::OFF_CHUNKLIQUID_HIGH);
        if (!staged.Culled(box))
            out.push_back(slot);
    }
    return out;
}

// A pass's slots (0x00685010, 0x006851B0, 0x006855A0 per slot): with world
// flag 0x1000000, the transform at the slot's chunk, GatherSlotLights'
// record (0x0068DF70) and the slot's draw.
void DrawSlots(const std::vector<void *> &slots, uint32_t pass, const Staged &staged, void *frame,
               const float (&eye)[3]) {
    if ((Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS) & Offsets::WORLD_FLAG_LIQUIDS) == 0)
        return;
    const float half = Game::Read<float>(Offsets::VAR_HALF);
    const float radius = Game::Read<float>(Offsets::VAR_LIQUID_LIGHT_RADIUS_IMMEDIATE);
    for (void *slot : slots) {
        const void *chunk = Game::Read<const void *>(slot, Offsets::OFF_CHUNKLIQUID_CHUNK);
        staged.PlaceAt(chunk);
        const float *centre = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_CENTRE);
        const float low = Game::Read<float>(slot, Offsets::OFF_CHUNKLIQUID_LOW);
        const float high = Game::Read<float>(slot, Offsets::OFF_CHUNKLIQUID_HIGH);
        const float sphere[4] = {centre[0], centre[1],
                                 static_cast<float>((static_cast<double>(high) - low) * half + low), radius};
        // The sun and the chunk's lights when the depth less twice the radius
        // is inside the fog end (0x0068E128).
        CommitRecord(frame, sphere, staged.Depth(sphere, radius) - radius < staged.fogEnd, eye);
        reinterpret_cast<void(__thiscall *)(void *, uint32_t)>(Offsets::FUN_CHUNKLIQUID_DRAW)(slot, pass);
    }
}

// The water passes' states (0x00685010 ocean, 0x006851B0 river): the swatch
// on stage 0, then the water program with its material, or the add combine.
// The river's leg for a third-stage program (0x00C800EC) is left out: nothing
// writes that program.
void WaterStates(uintptr_t swatch) {
    Gx::SetState(Offsets::GXRS_CULL, 0);
    Gx::SetState(Offsets::GXRS_TEXTURE0, Game::Read<void *>(swatch));
    if (Game::Read<uint8_t>(Offsets::VAR_FANCY_WATER) == 0) {
        Gx::SetState(Offsets::GXRS_PIXEL_SHADER, static_cast<const void *>(nullptr));
        Gx::SetState(Offsets::GXRS_COMBINER0 + 1, Offsets::LIQUID_STAGE1_ADD);
        return;
    }
    Gx::SetState(Offsets::GXRS_PIXEL_SHADER, Game::Read<void *>(Offsets::VAR_WATER_PROGRAM));
    Gx::SetStateWord(Offsets::GXRS_SPECULAR, 0xFFFFFFFFu);
    Gx::SetStateFloat(Offsets::GXRS_SHININESS, Game::Read<float>(Offsets::VAR_WATER_SHININESS));
    Gx::SetState(Offsets::GXRS_BLEND, Offsets::GXRS_BLEND_ALPHA);
    Gx::SetState(Offsets::GXRS_DEPTH_WRITE, 0);
}

} // namespace

void DrawMagma(Land &land, void *frame, const float (&eye)[3], float farClip) {
    if ((Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS) & Offsets::WORLD_FLAG_CHUNKS) == 0)
        return;
    const Staged staged(frame, eye, farClip);
    const std::vector<void *> slots = VisibleSlots(land, staged, Offsets::LIQUID_PASS_MAGMA);
    if (slots.empty())
        return;
    // DrawMagmaPass (0x006855A0): culling, lighting and blending off.
    const Gx::SavedStates states;
    Gx::SetState(Offsets::GXRS_CULL, 0);
    Gx::SetState(Offsets::GXRS_LIGHTING, 0);
    Gx::SetState(Offsets::GXRS_BLEND, 0);
    DrawSlots(slots, Offsets::LIQUID_PASS_MAGMA, staged, frame, eye);
}

void DrawWater(Land &land, void *frame, const float (&eye)[3], float farClip, uint32_t cameraLiquid) {
    if ((Game::Read<uint32_t>(Offsets::VAR_WORLD_FLAGS) & Offsets::WORLD_FLAG_CHUNKS) == 0)
        return;
    const Staged staged(frame, eye, farClip);
    const std::vector<void *> ocean = VisibleSlots(land, staged, Offsets::LIQUID_PASS_OCEAN);
    const std::vector<void *> river = VisibleSlots(land, staged, Offsets::LIQUID_PASS_RIVER);
    if (ocean.empty() && river.empty())
        return;
    // The swatches in the map's water colours at the eye: the first slot each
    // pass draws refills its swatch from the light state while its fresh flag
    // is clear (0x0068DE9E, 0x0068DEED); clearing them again after lets the
    // world's next frame refill them in its own.
    const World::MapWater water(land.mapID, cameraLiquid, eye);
    uint32_t &riverFresh = Game::Ref<uint32_t>(Offsets::VAR_RIVER_SWATCH_FRESH);
    uint32_t &oceanFresh = Game::Ref<uint32_t>(Offsets::VAR_OCEAN_SWATCH_FRESH);
    if (water.Swapped())
        riverFresh = oceanFresh = 0;
    // RenderLiquid's order (0x006816D0): the ocean, then the river.
    if (!ocean.empty()) {
        const Gx::SavedStates states;
        WaterStates(Offsets::VAR_OCEAN_SWATCH);
        DrawSlots(ocean, Offsets::LIQUID_PASS_OCEAN, staged, frame, eye);
    }
    if (!river.empty()) {
        const Gx::SavedStates states;
        WaterStates(Offsets::VAR_RIVER_SWATCH);
        DrawSlots(river, Offsets::LIQUID_PASS_RIVER, staged, frame, eye);
    }
    if (water.Swapped())
        riverFresh = oceanFresh = 0;
}

// ---- collision (layer 3) ------------------------------------------------------
//
// The world's terrain legs over the scene's chunks. The world finds a chunk
// through its global area table, so its own two functions cannot see these;
// both are transcribed with the arithmetic in the client's order (x87 at
// 53 bits, float stores where it stores), and call its helpers where they
// are pure: the trace's four cell walks and its ray-triangle test. A cell
// outside the scene's chunks has no ground: the world, which loads every
// tile it needs, fails a box query there and ends a trace; the scene's
// terrain ends at its radius, past which a WMO may still stand.

namespace {

double D(float v) { return static_cast<double>(v); }

// The cell of a distance from the map's origin (0x006AA9B4, 0x0069C375):
// the product stored as a float, less a half, fistp (nearest).
int32_t Cell(float perYard, float half, float distance) {
    const float product = static_cast<float>(D(perYard) * distance);
    return static_cast<int32_t>(std::lrint(D(product) - half));
}

void *ChunkAt(const Land &land, int32_t row, int32_t column) {
    if (row < 0 || column < 0)
        return nullptr;
    const auto found = land.byCell.find({static_cast<uint32_t>(row), static_cast<uint32_t>(column)});
    return found != land.byCell.end() ? found->second : nullptr;
}

const float *Vertices(const void *chunk) { return Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_VERTICES); }

uint16_t Holes(const void *chunk) {
    return Game::Read<uint16_t>(Game::Read<const uint8_t *>(chunk, Offsets::OFF_CMAPCHUNK_HEADER),
                                Offsets::OFF_MCNK_HOLES);
}

// The hole bit of a cell: (column >> 1) + (row >> 1) x 4 of the table.
bool CellIsHole(const void *chunk, int32_t row, int32_t column) {
    const uint32_t bit =
        Game::Read<uint32_t>(Offsets::VAR_HOLE_BITS + ((column >> 1) + (row >> 1) * 4) * sizeof(uint32_t));
    return (bit & Holes(chunk)) != 0;
}

// A wall facet: its axis normal, d = -n.p, and its vertices.
Contact::Facet WallFacet(const float (&n)[3], const float (&a)[3], const float (&b)[3], const float (&c)[3]) {
    Contact::Facet f;
    std::memcpy(f.plane.n, n, sizeof f.plane.n);
    f.plane.d = static_cast<float>(-((D(n[0]) * a[0] + D(n[1]) * a[1]) + D(n[2]) * a[2]));
    std::memcpy(f.v[0], a, sizeof f.v[0]);
    std::memcpy(f.v[1], b, sizeof f.v[1]);
    std::memcpy(f.v[2], c, sizeof f.v[2]);
    return f;
}

void Add3(const float (&a)[3], const float (&b)[3], float (&out)[3]) {
    for (int i = 0; i < 3; ++i)
        out[i] = static_cast<float>(D(a[i]) + b[i]);
}

// AddImpassableWalls (0x006AB530): a quad of two facets on each side of the
// chunk's box the query box crosses, in the order y-min, y-max, x-min, x-max.
void AddWalls(const void *chunk, const float (&box)[6], std::vector<Contact::Facet> &out) {
    const float *lo = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_BOX_MIN);
    const float *hi = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_BOX_MAX);
    const float up[3] = {0.0f, 0.0f, Game::Read<float>(Offsets::VAR_CHUNK_WALL_HEIGHT_IMMEDIATE)};
    const auto side = [&](const float (&p)[3], const float (&e)[3], const float (&n)[3]) {
        float pe[3], pu[3], peu[3];
        Add3(p, e, pe);
        Add3(pe, up, peu);
        Add3(p, up, pu);
        out.push_back(WallFacet(n, p, pe, peu));
        out.push_back(WallFacet(n, p, peu, pu));
    };
    if (box[1] < lo[1]) {
        const float p[3] = {lo[0], lo[1], lo[2]};
        const float e[3] = {static_cast<float>(D(hi[0]) - lo[0]), 0.0f, 0.0f};
        const float n[3] = {0.0f, -1.0f, 0.0f};
        side(p, e, n);
    }
    if (box[4] > hi[1]) {
        const float p[3] = {hi[0], hi[1], lo[2]};
        const float e[3] = {static_cast<float>(D(lo[0]) - hi[0]), 0.0f, 0.0f};
        const float n[3] = {0.0f, 1.0f, 0.0f};
        side(p, e, n);
    }
    if (box[0] < lo[0]) {
        const float p[3] = {lo[0], hi[1], lo[2]};
        const float e[3] = {0.0f, static_cast<float>(D(lo[1]) - hi[1]), 0.0f};
        const float n[3] = {-1.0f, 0.0f, 0.0f};
        side(p, e, n);
    }
    if (box[3] > hi[0]) {
        const float p[3] = {hi[0], lo[1], lo[2]};
        const float e[3] = {0.0f, static_cast<float>(D(hi[1]) - lo[1]), 0.0f};
        const float n[3] = {1.0f, 0.0f, 0.0f};
        side(p, e, n);
    }
}

// The outcode of a chunk-relative vertex against the shifted box (0x006AAF60):
// the sign bits of float(v - min + e), then of float(max - v + e).
uint32_t Outcode(const float *v, const float (&lo)[3], const float (&hi)[3], float e) {
    uint32_t code = 0;
    for (int i = 0; i < 3; ++i) {
        if (std::signbit(static_cast<float>((D(v[i]) - lo[i]) + e)))
            code |= 1u << i;
        if (std::signbit(static_cast<float>((D(hi[i]) - v[i]) + e)))
            code |= 8u << i;
    }
    return code;
}

// A chunk's part of a box query (GatherChunkFacets 0x006AAE2A): the query's
// cell rectangle clipped to the chunk's cells, {row first, column first, row
// last, column last}, and the box less the chunk's position.
struct Window {
    int32_t rect[4];
    float lo[3];
    float hi[3];
};

Window ChunkWindow(const void *chunk, int32_t row, int32_t column, const int32_t (&cells)[4],
                   const float (&box)[6]) {
    const int32_t last = Offsets::CHUNK_CELLS - 1;
    Window w = {{(std::max)(cells[0] - row * Offsets::CHUNK_CELLS, 0),
                 (std::max)(cells[1] - column * Offsets::CHUNK_CELLS, 0),
                 (std::min)(cells[2] - row * Offsets::CHUNK_CELLS, last),
                 (std::min)(cells[3] - column * Offsets::CHUNK_CELLS, last)},
                {},
                {}};
    const float *position = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_POSITION);
    for (int i = 0; i < 3; ++i) {
        const float shift = -position[i];
        w.lo[i] = static_cast<float>(D(box[i]) + shift);
        w.hi[i] = static_cast<float>(D(box[i + 3]) + shift);
    }
    return w;
}

bool Impassable(const void *chunk) {
    return (Game::Read<uint8_t>(chunk, Offsets::OFF_CMAPCHUNK_FLAGS) & Offsets::CMAPCHUNK_FLAG_IMPASSABLE) != 0;
}

// GatherChunkFacets' wall and triangle legs (0x006AAE2A..0x006AB3F1).
void GatherChunk(const void *chunk, int32_t row, int32_t column, const int32_t (&cells)[4], const float (&box)[6],
                 std::vector<Contact::Facet> &out) {
    if (Impassable(chunk))
        AddWalls(chunk, box, out);
    const Window w = ChunkWindow(chunk, row, column, cells, box);
    const int32_t rowFirst = w.rect[0], columnFirst = w.rect[1], rowLast = w.rect[2], columnLast = w.rect[3];
    const float *position = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_POSITION);
    const float(&lo)[3] = w.lo;
    const float(&hi)[3] = w.hi;
    const float e = Game::Read<float>(Offsets::VAR_GATHER_EPSILON);
    const auto *probe = reinterpret_cast<const int32_t *>(static_cast<uintptr_t>(Offsets::VAR_CELL_OUTCODE_VERTICES));
    const auto *triangles = reinterpret_cast<const int32_t *>(static_cast<uintptr_t>(Offsets::VAR_CELL_FACET_TRIANGLES));
    const float *vertices = Vertices(chunk);
    for (int32_t r = rowFirst; r <= rowLast; ++r) {
        for (int32_t c = columnFirst; c <= columnLast; ++c) {
            if (CellIsHole(chunk, r, c))
                continue;
            const float *corner = vertices + (r * Offsets::CHUNK_ROW_VERTICES + c) * 3;
            uint32_t codes[Offsets::CHUNK_ROW_VERTICES + 2] = {};
            for (int k = 0; k < Offsets::CELL_OUTCODE_VERTICES; ++k)
                codes[probe[k]] = Outcode(corner + probe[k] * 3, lo, hi, e);
            for (int t = 0; t < Offsets::CELL_TRIANGLES; ++t) {
                const int32_t *tri = triangles + t * 3;
                if ((codes[tri[0]] & codes[tri[1]] & codes[tri[2]]) != 0)
                    continue;
                float v[3][3];
                for (int k = 0; k < 3; ++k)
                    for (int i = 0; i < 3; ++i)
                        v[k][i] = static_cast<float>(D(corner[tri[k] * 3 + i]) + position[i]);
                out.push_back(Contact::MakeFacet(v[0], v[1], v[2]));
            }
        }
    }
}

} // namespace

namespace {

// The box query's cells (0x006AA92D): the distances from the origin, the
// bounds, the cells {max x, max y, min x, min y}. False outside the map.
bool BoxCells(const float (&box)[6], int32_t (&cells)[4]) {
    const float origin = Game::Read<float>(Offsets::VAR_MAP_ORIGIN);
    const float fromMaxX = static_cast<float>(-(D(box[3]) - origin));
    const double fromMaxY = -(D(box[4]) - origin);
    const float fromMinX = static_cast<float>(-(D(box[0]) - origin));
    const float fromMinY = static_cast<float>(-(D(box[1]) - origin));
    const float extent = Game::Read<float>(Offsets::VAR_MAP_EXTENT);
    if (fromMaxY < 0.0 || fromMaxX < 0.0f || fromMinY >= extent || fromMinX >= extent)
        return false;
    const float perYard = Game::Read<float>(Offsets::VAR_GATHER_CELLS_PER_YARD);
    const float half = Game::Read<float>(Offsets::VAR_GATHER_HALF_CELL);
    cells[0] = Cell(perYard, half, fromMaxX);
    cells[1] = Cell(perYard, half, static_cast<float>(fromMaxY));
    cells[2] = Cell(perYard, half, fromMinX);
    cells[3] = Cell(perYard, half, fromMinY);
    return true;
}

} // namespace

bool QueryFacets(Land &land, const float (&box)[6], std::vector<Contact::Facet> &out) {
    int32_t cells[4];
    if (!BoxCells(box, cells))
        return false;
    // One query id for every chunk's doodads (0x006AA8BF advances it).
    if (land.props != nullptr)
        World::BeginPropQuery(*land.props);
    bool complete = true;
    for (int32_t row = cells[0] >> 3; row <= cells[2] >> 3; ++row) {
        for (int32_t column = cells[1] >> 3; column <= cells[3] >> 3; ++column) {
            const void *chunk = ChunkAt(land, row, column);
            if (chunk == nullptr)
                continue;
            GatherChunk(chunk, row, column, cells, box, out);
            // GatherChunkFacets' doodad leg (0x006AB496), after its triangles;
            // a doodad still loading fails the chunk.
            const auto found = land.propsOf.find(chunk);
            if (land.props == nullptr || found == land.propsOf.end())
                continue;
            for (uint32_t prop : *found->second) {
                if (!World::AddPropFacets(*land.props, prop, box, out)) {
                    complete = false;
                    break;
                }
            }
        }
    }
    return complete;
}

bool CollectDecalBatches(const Land &land, const float (&box)[6], uint32_t mask) {
    // TestTerrainBox (0x006AD4E0): the cells, the first from the unrounded
    // distance (0x006AD525 multiplies before it stores).
    const float origin = Game::Read<float>(Offsets::VAR_MAP_ORIGIN);
    const float perYard = Game::Read<float>(Offsets::VAR_DECAL_CELLS_PER_YARD);
    const float half = Game::Read<float>(Offsets::VAR_DECAL_HALF_CELL);
    const float fromMaxX = static_cast<float>(-(D(box[3]) - origin));
    const float fromMaxY = static_cast<float>(-(D(box[4]) - origin));
    const float fromMinX = static_cast<float>(-(D(box[0]) - origin));
    const double fromMinY = -(D(box[1]) - origin);
    const int32_t minY = static_cast<int32_t>(std::lrint(D(static_cast<float>(D(perYard) * fromMinY)) - half));
    const int32_t cells[4] = {Cell(perYard, half, fromMaxX), Cell(perYard, half, fromMaxY),
                              Cell(perYard, half, fromMinX), minY};
    using Triangles_t = uint8_t(__fastcall *)(void *chunk, const int32_t *rect, const float *box, uint32_t unused);
    using Gather_t = uint8_t(__thiscall *)(void *slot, void *unused, const float *box, const int32_t *cells);
    const int32_t last = Offsets::CHUNK_CELLS - 1;
    bool any = false;
    for (int32_t row = cells[0] >> 3; row <= cells[2] >> 3; ++row) {
        for (int32_t column = cells[1] >> 3; column <= cells[3] >> 3; ++column) {
            void *chunk = ChunkAt(land, row, column);
            if (chunk == nullptr)
                continue;
            // TestChunkBox (0x006AD610): the box less the chunk position, the
            // rectangle clipped to the chunk's cells.
            const float *position = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_POSITION);
            float local[6];
            for (int i = 0; i < 3; ++i) {
                const float shift = -position[i];
                local[i] = static_cast<float>(D(box[i]) + shift);
                local[i + 3] = static_cast<float>(D(box[i + 3]) + shift);
            }
            const int32_t rect[4] = {(std::max)(cells[0] - row * Offsets::CHUNK_CELLS, 0),
                                     (std::max)(cells[1] - column * Offsets::CHUNK_CELLS, 0),
                                     (std::min)(cells[2] - row * Offsets::CHUNK_CELLS, last),
                                     (std::min)(cells[3] - column * Offsets::CHUNK_CELLS, last)};
            if ((mask & Offsets::CHUNK_TRIANGLE_CLASS) != 0 &&
                reinterpret_cast<Triangles_t>(Offsets::FUN_TEST_CHUNK_TRIANGLES)(chunk, rect, local, 0) != 0)
                any = true;
            // The liquid leg (0x006AD75E): each slot the class names into the
            // same batches.
            for (uint32_t i = 0; i < Offsets::CHUNK_LIQUID_SLOTS; ++i) {
                void *slot = LiquidSlot(chunk, i);
                if (slot == nullptr || (mask & (1u << (Offsets::LIQUID_CLASS_FIRST_BIT + i))) == 0)
                    continue;
                uint32_t unused = 0;
                if (reinterpret_cast<Gather_t>(Offsets::FUN_CHUNKLIQUID_GATHER_CELL_FACETS)(slot, &unused, local,
                                                                                             rect) != 0)
                    any = true;
            }
        }
    }
    return any;
}

bool GroundEffect(const Land &land, const float (&point)[3], int32_t &terrainType) {
    // GetGroundEffect (0x0069B1C0) over the scene's chunks.
    const float origin = Game::Read<float>(Offsets::VAR_MAP_ORIGIN);
    const float fromY = static_cast<float>(-(D(point[1]) - origin));
    const float fromX = static_cast<float>(-(D(point[0]) - origin));
    const float extent = Game::Read<float>(Offsets::VAR_MAP_EXTENT);
    if (!(fromY >= 0.0f) || !(fromX >= 0.0f) || !(fromY < extent) || !(fromX < extent))
        return false;
    const float perYard = Game::Read<float>(Offsets::VAR_POINT_CELLS_PER_YARD);
    const float half = Game::Read<float>(Offsets::VAR_POINT_HALF_CELL);
    const int32_t cellColumn = Cell(perYard, half, fromY);
    const int32_t cellRow = Cell(perYard, half, fromX);
    const void *chunk = ChunkAt(land, cellRow >> 3, cellColumn >> 3);
    if (chunk == nullptr)
        return false;
    const int32_t column = cellColumn & 7, row = cellRow & 7;
    const uint32_t layers = Game::Read<uint32_t>(chunk, Offsets::OFF_CMAPCHUNK_LAYER_COUNT);
    if (CellIsHole(chunk, row, column) || layers == 0)
        return false;
    const uint16_t map = Game::Read<const uint16_t *>(chunk, Offsets::OFF_CMAPCHUNK_LOW_TEXTURES)[row];
    const uint16_t mask = Game::Read<uint16_t>(Offsets::VAR_LOW_TEXTURE_MASKS + column * sizeof(uint16_t));
    const uint32_t shift = Game::Read<uint32_t>(Offsets::VAR_LOW_TEXTURE_SHIFTS + column * sizeof(uint32_t));
    const uint32_t index = static_cast<uint32_t>(map & mask) >> shift;
    // The client reads whichever layer the map names; one the chunk lacks
    // would be a null record there.
    if (index >= layers)
        return false;
    const void *layer = Game::Read<const void *>(chunk, Offsets::OFF_CMAPCHUNK_LAYERS + index * sizeof(void *));
    if (layer == nullptr)
        return false;
    const uint16_t effect = Game::Read<uint16_t>(layer, Offsets::OFF_CHUNKLAYER_EFFECT);
    if (effect == Offsets::GROUND_EFFECT_NONE)
        return false;
    const uint8_t *row0 = DBC::Record(Offsets::VAR_GROUNDEFFECTTEXTURE_RECORDS, Offsets::VAR_GROUNDEFFECTTEXTURE_COUNT,
                                      effect);
    if (row0 == nullptr)
        return false;
    terrainType = Game::Read<int32_t>(row0, Offsets::OFF_GROUNDEFFECTTEXTURE_TERRAIN_TYPE);
    return true;
}

bool LiquidAt(const Land &land, const float (&point)[3], uint32_t &type, float &surface) {
    // GetLiquidStatus (0x0069B709): u from y and v from x, each the negated
    // distance from the origin times the cells per yard, stored as a float;
    // the cells those less a half, rounded; the fractions the stored values
    // less their cells, stored.
    const double origin = Game::Read<float>(Offsets::VAR_MAP_ORIGIN);
    const double perYard = Game::Read<float>(Offsets::VAR_POINT_CELLS_PER_YARD);
    const double half = Game::Read<float>(Offsets::VAR_POINT_HALF_CELL);
    const float u = static_cast<float>(-(D(point[1]) - origin) * perYard);
    const float v = static_cast<float>(-(D(point[0]) - origin) * perYard);
    const int32_t cellU = static_cast<int32_t>(std::lrint(D(u) - half));
    const int32_t cellV = static_cast<int32_t>(std::lrint(D(v) - half));
    void *chunk = ChunkAt(land, cellV >> 3, cellU >> 3);
    if (chunk == nullptr)
        return false;
    const float fu = static_cast<float>(D(u) - cellU);
    const float fv = static_cast<float>(D(v) - cellV);
    const uint32_t column = static_cast<uint32_t>(cellU) & 7, row = static_cast<uint32_t>(cellV) & 7;
    const double slack = Game::Read<float>(Offsets::VAR_ONE_HUNDREDTH);
    for (uint32_t i = 0; i < Offsets::CHUNK_LIQUID_SLOTS; ++i) {
        const void *slot = LiquidSlot(chunk, i);
        if (slot == nullptr)
            continue;
        const uint8_t tile =
            Game::Read<const uint8_t *>(slot, Offsets::OFF_CHUNKLIQUID_TILES)[row * Offsets::LIQUID_TILE_ROW + column];
        if ((tile & Offsets::LIQUID_TILE_TYPE_MASK) == Offsets::LIQUID_TILE_NONE)
            continue;
        const uint32_t kind = tile & Offsets::LIQUID_TYPE_MASK;
        // The ocean is at height 0: the point is in it under the slack.
        if (kind == Offsets::LIQUID_OCEAN) {
            if (D(point[2]) < slack) {
                type = kind;
                surface = 0.0f;
                return true;
            }
            continue;
        }
        // The MCLQ heights bilinear along u (fu), then v (fv), at 53 bits.
        const auto *records = Game::Read<const uint8_t *>(slot, Offsets::OFF_CHUNKLIQUID_MCLQ_VERTICES);
        const uint32_t first = row * Offsets::LIQUID_VERTEX_ROW + column;
        const auto h = [&](uint32_t k) {
            return D(Game::Read<float>(records, k * Offsets::MCLQ_VERTEX_SIZE + Offsets::OFF_MCLQ_VERTEX_HEIGHT));
        };
        const uint32_t next = first + Offsets::LIQUID_VERTEX_ROW;
        const double a = (h(first + 1) - h(first)) * fu + h(first);
        const double b = (h(next + 1) - h(next)) * fu + h(next);
        const float height = static_cast<float>((b - a) * fv + a);
        if (D(height) + slack > D(point[2])) {
            type = kind;
            surface = height;
            return true;
        }
    }
    return false;
}

bool QueryLiquidFacets(const Land &land, const float (&box)[6], std::vector<Contact::Facet> &out) {
    using Gather_t = uint8_t(__thiscall *)(void *slot, void *unused, const float *box, const int32_t *cells);
    int32_t cells[4];
    if (!BoxCells(box, cells))
        return false;
    for (int32_t row = cells[0] >> 3; row <= cells[2] >> 3; ++row) {
        for (int32_t column = cells[1] >> 3; column <= cells[3] >> 3; ++column) {
            void *chunk = ChunkAt(land, row, column);
            if (chunk == nullptr)
                continue;
            // The walls ignore the class (0x006AAE09).
            if (Impassable(chunk))
                AddWalls(chunk, box, out);
            // The liquid leg (0x006AB3F7): each slot the class names, through
            // GatherLiquidFacets (0x006AB4E0).
            const Window w = ChunkWindow(chunk, row, column, cells, box);
            const float local[6] = {w.lo[0], w.lo[1], w.lo[2], w.hi[0], w.hi[1], w.hi[2]};
            for (uint32_t i = 0; i < Offsets::CHUNK_LIQUID_SLOTS; ++i) {
                void *slot = LiquidSlot(chunk, i);
                if (slot == nullptr ||
                    (Offsets::WORLD_CLASS_SWIM_LIQUID & (1u << (Offsets::LIQUID_CLASS_FIRST_BIT + i))) == 0)
                    continue;
                World::ResetFacetBatches();
                uint32_t unused = 0;
                if (reinterpret_cast<Gather_t>(Offsets::FUN_CHUNKLIQUID_GATHER_CELL_FACETS)(slot, &unused, local,
                                                                                             w.rect) != 0)
                    World::EmitFacetBatches(out);
            }
        }
    }
    return true;
}

namespace {

struct Ray {
    float origin[3];
    float direction[3];
};

// TraceTerrainSegment's walk (0x0069C320..0x0069C4B8): the ends to cells,
// then the engine's own walks fill its cell list.
void ListCells(const float (&start)[3], const float (&end)[3]) {
    const float origin = Game::Read<float>(Offsets::VAR_MAP_ORIGIN);
    const float startY = static_cast<float>(-(D(start[1]) - origin));
    const float startX = static_cast<float>(-(D(start[0]) - origin));
    const float endY = static_cast<float>(-(D(end[1]) - origin));
    const float endX = static_cast<float>(-(D(end[0]) - origin));
    const float dy = static_cast<float>(D(endY) - startY);
    const float dx = static_cast<float>(D(endX) - startX);
    const float perYard = Game::Read<float>(Offsets::VAR_TRACE_CELLS_PER_YARD);
    const float half = Game::Read<float>(Offsets::VAR_TRACE_HALF_CELL);
    int32_t cells[4] = {Cell(perYard, half, startX), Cell(perYard, half, startY), Cell(perYard, half, endX),
                        Cell(perYard, half, endY)};
    Game::Ref<uint32_t>(Offsets::VAR_TRACE_CELL_COUNT) = 0;
    const float epsilon = Game::Read<float>(Offsets::VAR_TRACE_RUN_EPSILON);
    const float spanY = std::fabs(dy);
    const float spanX = std::fabs(dx);
    using Run_t = void(__thiscall *)(int32_t *cells);
    using Line_t = void(__fastcall *)(float *start, const float *end, int32_t *cells);
    if (spanY < epsilon || cells[1] == cells[3]) {
        reinterpret_cast<Run_t>(Offsets::FUN_LIST_CELL_COLUMN_RUN)(cells);
    } else if (spanX < epsilon || cells[0] == cells[2]) {
        reinterpret_cast<Run_t>(Offsets::FUN_LIST_CELL_ROW_RUN)(cells);
    } else {
        float from[3] = {startY, startX, 0.0f};
        const float to[3] = {endY, endX, 0.0f};
        reinterpret_cast<Line_t>(spanY > spanX ? Offsets::FUN_LIST_CELLS_ALONG_ROWS
                                               : Offsets::FUN_LIST_CELLS_ALONG_COLUMNS)(from, to, cells);
    }
}

} // namespace

bool Trace(const Land &land, const float (&start)[3], const float (&end)[3], float &fraction, float (&normal)[3]) {
    ListCells(start, end);
    // TraceListedCells (0x0069C920), its terrain triangles.
    const uint32_t *list = Game::Read<const uint32_t *>(Offsets::VAR_TRACE_CELLS);
    uint32_t left = Game::Read<uint32_t>(Offsets::VAR_TRACE_CELL_COUNT);
    if (list == nullptr || left == 0)
        return false;
    const double dx = D(end[0]) - start[0], dy = D(end[1]) - start[1], dz = D(end[2]) - start[2];
    const float dzf = static_cast<float>(dz);
    const float k = static_cast<float>(1.0 / std::sqrt((dz * dzf + dy * dy) + dx * dx));
    Ray ray{{start[0], start[1], start[2]},
            {static_cast<float>(dx * k), static_cast<float>(dy * k), static_cast<float>(D(dzf) * k)}};
    float best = fraction;
    const void *hitChunk = nullptr;
    uint32_t hitIndices[3] = {};
    const void *chunk = nullptr;
    // The walk's chunk keys start from the first pair crossed, as 0x0069C932
    // loads them.
    uint32_t seenA = list[1] & Offsets::TRACE_CELL_LIMIT;
    uint32_t seenB = list[0] & Offsets::TRACE_CELL_LIMIT;
    const auto *pairs = reinterpret_cast<const int32_t *>(static_cast<uintptr_t>(Offsets::VAR_CELL_TRACE_TRIANGLES));
    const float slack = Game::Read<float>(Offsets::VAR_TRACE_SLACK_IMMEDIATE);
    using Intersect_t = uint8_t(__fastcall *)(const Ray *ray, const float *vertices, const uint32_t *indices,
                                              float *t, float *uv, float slack);
    for (; left != 0; list += 2) {
        const uint32_t a = list[0], b = list[1]; // the y cell, then the x cell
        left -= 2;
        if (a > Offsets::TRACE_CELL_LIMIT || b > Offsets::TRACE_CELL_LIMIT)
            break;
        if ((a & Offsets::TRACE_CELL_CHUNK_BITS) != seenA || (b & Offsets::TRACE_CELL_CHUNK_BITS) != seenB) {
            chunk = ChunkAt(land, static_cast<int32_t>(b >> 3), static_cast<int32_t>(a >> 3));
            if (chunk == nullptr)
                break;
            seenA = a & Offsets::TRACE_CELL_CHUNK_BITS;
            seenB = b & Offsets::TRACE_CELL_CHUNK_BITS;
            const float *position = Game::Ptr<float>(chunk, Offsets::OFF_CMAPCHUNK_POSITION);
            for (int i = 0; i < 3; ++i)
                ray.origin[i] = static_cast<float>(D(start[i]) - position[i]);
        }
        if (chunk == nullptr)
            break;
        const int32_t column = static_cast<int32_t>(a & 7), row = static_cast<int32_t>(b & 7);
        if (CellIsHole(chunk, row, column))
            continue;
        const uint32_t base = static_cast<uint32_t>(row * Offsets::CHUNK_ROW_VERTICES + column);
        for (int t = 0; t < Offsets::CELL_TRIANGLES; ++t) {
            const uint32_t indices[3] = {base + Offsets::CELL_CENTRE_VERTEX, base + pairs[t * 2 + 1],
                                         base + pairs[t * 2]};
            float raw = 0.0f;
            if (reinterpret_cast<Intersect_t>(Offsets::FUN_INTERSECT_RAY_TRIANGLE)(&ray, Vertices(chunk), indices,
                                                                                  &raw, nullptr, slack) == 0)
                continue;
            const double scaled = D(raw) * k;
            if (scaled < best && !(scaled < 0.0)) {
                best = static_cast<float>(scaled);
                hitChunk = chunk;
                std::memcpy(hitIndices, indices, sizeof hitIndices);
            }
        }
    }
    if (!(best < fraction) || hitChunk == nullptr)
        return false;
    fraction = best;
    // The hit triangle's plane (PlaneFromTriangle, as the terrain first hit
    // 0x006AA600 records it), turned to face the start.
    const float *v = Vertices(hitChunk);
    Contact::Plane plane;
    using Plane_t = void(__fastcall *)(Contact::Plane *, int, const float *, const float *, const float *);
    reinterpret_cast<Plane_t>(Offsets::FUN_PLANE_FROM_TRIANGLE)(&plane, 0, v + hitIndices[0] * 3,
                                                                v + hitIndices[1] * 3, v + hitIndices[2] * 3);
    const double facing = (D(plane.n[0]) * dx + D(plane.n[1]) * dy) + D(plane.n[2]) * dz;
    for (int i = 0; i < 3; ++i)
        normal[i] = facing > 0.0 ? -plane.n[i] : plane.n[i];
    return true;
}

} // namespace Model::Scene::Terrain
