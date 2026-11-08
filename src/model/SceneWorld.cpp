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

// A ModelScene's WMO (scene:SetWorldModel / SetWorldModelByMapID, a
// ClassicAPI extension; research in docs/notes/model-scene.md "Extension 8").
//
// The map object is the world's own: CMapObj::Get loads it into the engine's
// cache with one reference ours, its groups load as the world's map update
// loads them, and the paint draws it the way DrawMapObjectPass (0x00684840)
// draws one instance. The scene's view has no translation and its vertices
// are relative to the eye, as the world's are, so the same chain fits:
//
//   - render states pushed; world stack 8 = placement x Translate(-eye);
//   - the frustum stack pushed and a volume of the scene's own view stored
//     on it (BuildFrustumCorners over the device view moved by -eye and the
//     device projection, as the world's camera capture builds its own,
//     0x00680BC0), then moved into the object's space (TransformFrustum with
//     the inverse placement), which the drawers cull each batch against;
//   - a lighting record at the object's sphere: the scene's lights for that
//     point (M2 lights of the scene's models), the scene light as the sun
//     (the world adds its sun, 0x006A1F90), and the fog; committed at the
//     eye with fog, as the world commits it at the camera;
//   - each parsed group: the portal vertex fade once, then the interior or
//     exterior drawer, called directly (the world's drawer pointers are set
//     only by its own frame, 0x006B3890).
//
// Fog: the world fogs a WMO's insides with its own MFOG records once the
// camera is in it, but only for a WMO with more than one record
// (0x0069DE20); with no candidate fog in the camera's group the fog is record
// 0, its end capped at the far clip and its start the record's fraction of
// that (benilla lighting/fog "Interior fog"). Other WMOs take the zone's fog,
// which in a scene is the scene's own (SetFogColor and so on), staged with
// the scene light by the frame's light callback.
//
// The drawers read two world globals. The group fog flag would replace that
// fog with the light state's interior fog, so it is 0 while the scene draws.
// Window materials (flag 0x20) overwrite the sun's colours, then draw, then
// write the day's colours back; the sun pointer is NULL at the glue screen
// until a world loads, so while the scene draws it points at a copy of the
// scene light, the scene's stand-in for the sun.
//
// Lifetime: the reference keeps the object; each paint keeps every group
// alive for 30 s and queues the read of any the world frame has freed
// (0x00698800). The engine's reader ends the program when a file does not
// open and parses the root's chunks in a fixed order without reading their
// tags, so a root is refused unless all its chunks are in that order inside
// the file, it has at most 384 groups and each group file opens.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "gx/Device.h"
#include "model/M2.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Model::Scene::World {

struct Backdrop {
    void *object; // CMapObj, one reference ours
    float placement[16];
    float inverse[16];
    uint32_t doodadSet;
};

namespace {

using Matrix = float[16];

constexpr uint32_t Tag(const char (&name)[5]) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(name[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(name[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(name[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(name[3]));
}

struct ChunkHeader {
    uint32_t tag;
    uint32_t size;
};

// ---- files ------------------------------------------------------------------

class File {
  public:
    explicit File(const char *path) {
        using Open_t = int(__stdcall *)(const char *path, void **file);
        if (reinterpret_cast<Open_t>(Offsets::FUN_SFILE_OPEN)(path, &handle_) == 0)
            handle_ = nullptr;
    }
    ~File() {
        if (handle_ != nullptr)
            reinterpret_cast<uint32_t(__stdcall *)(void *)>(Offsets::FUN_SFILE_CLOSE)(handle_);
    }
    File(const File &) = delete;
    File &operator=(const File &) = delete;

    bool IsOpen() const { return handle_ != nullptr; }
    uint32_t Size() const {
        using Size_t = uint32_t(__stdcall *)(void *file, uint32_t *high);
        return reinterpret_cast<Size_t>(Offsets::FUN_SFILE_GET_FILE_SIZE)(handle_, nullptr);
    }
    bool Read(void *buffer, uint32_t size) {
        using Read_t = int(__stdcall *)(void *file, void *buffer, uint32_t size, uint32_t *read,
                                        void *overlapped, int);
        uint32_t read = 0;
        return handle_ != nullptr &&
               reinterpret_cast<Read_t>(Offsets::FUN_SFILE_READ)(handle_, buffer, size, &read, nullptr,
                                                                0) != 0 &&
               read == size;
    }
    bool Chunk(const char (&tag)[5], ChunkHeader &header) {
        return Read(&header, sizeof header) && header.tag == Tag(tag);
    }

  private:
    void *handle_ = nullptr;
};

bool HasWmoSuffix(const char *path) {
    const size_t n = std::strlen(path);
    return n > 4 && _stricmp(path + n - 4, ".wmo") == 0;
}

// CMapObj::LoadGroupFile's name (0x006C3E20): the root's up to its last '.',
// then "_%03d" and ".wmo".
std::string GroupPath(const char *root, uint32_t index) {
    std::string path(root);
    path.resize(path.rfind('.'));
    char suffix[16];
    std::snprintf(suffix, sizeof suffix, "_%03u.wmo", index);
    return path + suffix;
}

// The root chunks in the order CMapObj::ParseChunks (0x006C3A60) takes them
// after the version chunk.
constexpr const char kRootChunks[][5] = {"MOHD", "MOTX", "MOMT", "MOGN", "MOGI", "MOSB",
                                         "MOPV", "MOPT", "MOPR", "MOVV", "MOVB", "MOLT",
                                         "MODS", "MODN", "MODD", "MFOG"};

// The root's group count when the engine can read it safely, else -1.
int CheckedGroupCount(const char *path) {
    File file(path);
    if (!file.IsOpen())
        return -1;
    const uint32_t size = file.Size();
    std::vector<uint8_t> data(size);
    if (size < sizeof(ChunkHeader) + 4 || !file.Read(data.data(), size))
        return -1;
    const auto header = [&](size_t at) {
        ChunkHeader h{};
        std::memcpy(&h, data.data() + at, sizeof h);
        return h;
    };
    if (header(0).tag != Tag("MVER") || header(0).size != 4)
        return -1;
    size_t at = sizeof(ChunkHeader) + 4;
    int groups = -1;
    for (const auto &tag : kRootChunks) {
        if (at + sizeof(ChunkHeader) > size)
            return -1;
        const ChunkHeader h = header(at);
        if (h.tag != Tag(tag) || h.size > size - at - sizeof(ChunkHeader))
            return -1;
        if (h.tag == Tag("MOGI"))
            groups = static_cast<int>(h.size / Offsets::MOGI_RECORD_SIZE);
        at += sizeof(ChunkHeader) + h.size;
    }
    if (groups > Offsets::MAPOBJ_MAX_GROUPS)
        return -1;
    for (int i = 0; i < groups; ++i) {
        if (!File(GroupPath(path, static_cast<uint32_t>(i)).c_str()).IsOpen())
            return -1;
    }
    return groups;
}

// ---- matrices --------------------------------------------------------------------

void Identity(Matrix &m) {
    std::memset(m, 0, sizeof m);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void Translate(Matrix &m, const float (&t)[3]) {
    reinterpret_cast<void(__thiscall *)(float *, const float *)>(Offsets::FUN_MATRIX_TRANSLATE)(m, t);
}

void Rotate(Matrix &m, float angle, const float (&axis)[3]) {
    reinterpret_cast<void(__thiscall *)(float *, float, const float *, int)>(Offsets::FUN_MATRIX_ROTATE)(
        m, angle, axis, 1);
}

void Multiply(Matrix &out, const Matrix &a, const Matrix &b) {
    reinterpret_cast<float *(__fastcall *)(float *, const float *, const float *)>(
        Offsets::FUN_MATRIX_MULTIPLY)(out, a, b);
}

// ---- the draw ---------------------------------------------------------------------

void *Group(void *object, uint32_t index) {
    using Get_t = void *(__thiscall *)(void *object, uint32_t index, int force);
    return reinterpret_cast<Get_t>(Offsets::FUN_CMAPOBJ_GET_LOADED_GROUP)(object, index, 1);
}

// The world's map update for a group it may draw (0x00698800): alive for
// another 30 s, and its file read queued when the world frame freed it.
// True when it is parsed.
bool KeepGroup(void *object, uint32_t index, void *group) {
    Game::Ref<float>(group, Offsets::OFF_CMAPOBJGROUP_KEEP_ALIVE) =
        static_cast<float>(Offsets::MAPOBJ_GROUP_KEEP_ALIVE_SECONDS);
    if (Game::Read<uint8_t>(group, Offsets::OFF_CMAPOBJGROUP_PARSED) != 0)
        return true;
    if (Game::Read<void *>(group, Offsets::OFF_CMAPOBJGROUP_READ) == nullptr)
        reinterpret_cast<void(__thiscall *)(void *, uint32_t)>(Offsets::FUN_CMAPOBJ_LOAD_GROUP)(object, index);
    return false;
}

// A volume of the device's view and projection in scene space.
void BuildViewVolume(uint8_t (&volume)[Offsets::FRUSTUM_VOLUME_SIZE], const float (&eye)[3]) {
    Matrix view, projection;
    Gx::GetView(view);
    Gx::GetProjection(projection);
    Translate(view, {-eye[0], -eye[1], -eye[2]});
    using Corners_t = void(__fastcall *)(const float *view, const float *projection, float *corners);
    reinterpret_cast<Corners_t>(Offsets::FUN_FRUSTUM_CORNERS)(
        view, projection, reinterpret_cast<float *>(volume + Offsets::OFF_FRUSTUM_VOLUME_CORNERS));
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_VOLUME_BUILD_PLANES)(volume);
}

// The record's fog: record 0 of a WMO that has its own fog, else the scene's
// (with the scene light, by the frame's callback, as a scene model gets them).
void StageLightAndFog(Backdrop &b, void *record, void *frame, float farClip) {
    void *object = b.object;
    if (Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_FOG_COUNT) <= 1) {
        using Callback_t = void(__fastcall *)(void *instance, void *record, void *frame);
        reinterpret_cast<Callback_t>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB)(nullptr, record, frame);
        return;
    }
    reinterpret_cast<void(__thiscall *)(void *, void *)>(Offsets::FUN_M2LIGHTING_ADD_LIGHT)(
        record, Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT));
    const uint8_t *fog = Game::Read<const uint8_t *>(object, Offsets::OFF_CMAPOBJ_FOGS);
    float end = Game::Read<float>(fog, Offsets::OFF_MAPOBJ_FOG_END);
    if (!(end < farClip))
        end = farClip;
    const float start = Game::Read<float>(fog, Offsets::OFF_MAPOBJ_FOG_START_FRACTION) * end;
    const uint32_t color = Game::Read<uint32_t>(fog, Offsets::OFF_MAPOBJ_FOG_COLOR);
    const float rgb[3] = {static_cast<float>((color >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((color >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(color & 0xFF) / 255.0f};
    using SetFog_t = void(__thiscall *)(void *record, const float *rgb, float start, float end);
    reinterpret_cast<SetFog_t>(Offsets::FUN_M2LIGHTING_SET_FOG)(record, rgb, start, end);
}

void CommitLighting(Backdrop &b, void *frame, const float (&eye)[3], float farClip) {
    alignas(16) uint8_t record[Offsets::M2_LIGHTING_RECORD_SIZE];
    float sphere[4] = {};
    reinterpret_cast<float *(__thiscall *)(void *, float *)>(Offsets::FUN_CMAPOBJ_GET_BOUNDING_SPHERE)(
        b.object, sphere);
    const Matrix &m = b.placement;
    const float centre[4] = {sphere[0] * m[0] + sphere[1] * m[4] + sphere[2] * m[8] + m[12],
                             sphere[0] * m[1] + sphere[1] * m[5] + sphere[2] * m[9] + m[13],
                             sphere[0] * m[2] + sphere[1] * m[6] + sphere[2] * m[10] + m[14], sphere[3]};
    reinterpret_cast<void(__thiscall *)(void *, void *, const float *)>(Offsets::FUN_M2LIGHTING_INIT)(
        record, nullptr, centre);
    reinterpret_cast<void(__thiscall *)(void *, void *)>(Offsets::FUN_M2SCENE_ADD_LIGHTS_FOR_POINT)(
        Model::Pane::Scene(frame), record);
    StageLightAndFog(b, record, frame, farClip);
    using Commit_t = void(__thiscall *)(void *record, const float *origin, int fog);
    reinterpret_cast<Commit_t>(Offsets::FUN_M2LIGHTING_SET_DEVICE_LIGHTS)(record, eye, 1);
}

void DrawGroups(Backdrop &b, void *frame) {
    using Fix_t = void(__thiscall *)(void *object, void *group);
    using Draw_t = void(__thiscall *)(void *object, void *group, uint32_t again);
    void *object = b.object;
    uint32_t &fogFlag = Game::Ref<uint32_t>(Offsets::VAR_MAPOBJ_GROUP_FOG_FLAG);
    void *&sun = Game::Ref<void *>(Offsets::VAR_MAP_SUN);
    const uint32_t savedFogFlag = fogFlag;
    void *const savedSun = sun;
    alignas(16) uint8_t sunStandIn[Offsets::OFF_MAP_SUN_LIGHT + Offsets::M2LIGHT_SIZE] = {};
    std::memcpy(sunStandIn + Offsets::OFF_MAP_SUN_LIGHT, Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT),
                Offsets::M2LIGHT_SIZE);
    fogFlag = 0;
    sun = sunStandIn;
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        void *group = Group(object, i);
        if (group == nullptr || Game::Read<uint8_t>(group, Offsets::OFF_CMAPOBJGROUP_PARSED) == 0)
            continue;
        if (Game::Read<uint8_t>(group, Offsets::OFF_CMAPOBJGROUP_VERTEX_FADE_DONE) == 0)
            reinterpret_cast<Fix_t>(Offsets::FUN_CMAPOBJGROUP_FIX_VERTEX_ALPHA)(object, group);
        const bool exterior =
            (Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_FLAGS) & Offsets::MAPOBJ_GROUP_EXTERIOR_MASK) != 0;
        reinterpret_cast<Draw_t>(exterior ? Offsets::FUN_MAPOBJ_DRAW_EXTERIOR_GROUP
                                          : Offsets::FUN_MAPOBJ_DRAW_INTERIOR_GROUP)(object, group, 0);
    }
    sun = savedSun;
    fogFlag = savedFogFlag;
}

} // namespace

bool IsWmoPath(const char *path) { return path != nullptr && HasWmoSuffix(path); }

Backdrop *Load(const char *path, const float (&placement)[16], uint32_t doodadSet) {
    if (!IsWmoPath(path) || CheckedGroupCount(path) < 0)
        return nullptr;
    void *object = reinterpret_cast<void *(__fastcall *)(const char *)>(Offsets::FUN_CMAPOBJ_GET)(path);
    if (object == nullptr)
        return nullptr;
    auto *b = new Backdrop{object, {}, {}, doodadSet};
    std::memcpy(b->placement, placement, sizeof b->placement);
    reinterpret_cast<void(__thiscall *)(const float *, float *, float)>(Offsets::FUN_MATRIX_INVERT_AFFINE)(
        b->placement, b->inverse, 1.0f);
    return b;
}

// CMap::LoadWdt's read (0x00694760) and PlaceMapObjDef's placement
// (0x00695650) with the zero origin the WDT passes.
Backdrop *LoadMap(uint32_t mapID) {
    const char *dir =
        DBC::StringField(Offsets::VAR_MAP_RECORDS, Offsets::VAR_MAP_COUNT, mapID, Offsets::OFF_MAP_DIRECTORY);
    if (dir == nullptr || *dir == '\0')
        return nullptr;
    char wdtPath[260];
    std::snprintf(wdtPath, sizeof wdtPath, "World\\Maps\\%s\\%s.wdt", dir, dir);
    File wdt(wdtPath);
    ChunkHeader h{};
    uint32_t version = 0;
    uint8_t mphd[Offsets::WDT_MPHD_SIZE];
    std::vector<uint8_t> main(Offsets::WDT_MAIN_SIZE);
    if (!wdt.Chunk("MVER", h) || !wdt.Read(&version, sizeof version) || !wdt.Chunk("MPHD", h) ||
        !wdt.Read(mphd, sizeof mphd) || !wdt.Chunk("MAIN", h) || !wdt.Read(main.data(), Offsets::WDT_MAIN_SIZE))
        return nullptr;
    uint32_t flags = 0;
    std::memcpy(&flags, mphd, sizeof flags);
    if ((flags & Offsets::WDT_FLAG_MAP_OBJECT) == 0 || !wdt.Chunk("MWMO", h) || h.size == 0 || h.size > 260)
        return nullptr;
    std::vector<char> name(h.size + 1, '\0');
    uint8_t entry[Offsets::MODF_SIZE];
    if (!wdt.Read(name.data(), h.size) || !wdt.Chunk("MODF", h) || !wdt.Read(entry, sizeof entry))
        return nullptr;
    float pos[3], rot[3];
    uint16_t doodadSet = 0;
    std::memcpy(pos, entry + Offsets::OFF_MODF_POSITION, sizeof pos);
    std::memcpy(rot, entry + Offsets::OFF_MODF_ROTATION, sizeof rot);
    std::memcpy(&doodadSet, entry + Offsets::OFF_MODF_DOODAD_SET, sizeof doodadSet);
    const float toRadians = Game::Read<float>(Offsets::VAR_DEGREES_TO_RADIANS);
    const float pi = Game::Read<float>(Offsets::VAR_PI);
    Matrix m;
    Identity(m);
    Translate(m, {-pos[2], -pos[0], pos[1]});
    Rotate(m, rot[1] * toRadians + pi, {0.0f, 0.0f, 1.0f});
    Rotate(m, rot[0] * toRadians, {0.0f, 1.0f, 0.0f});
    Rotate(m, rot[2] * toRadians, {1.0f, 0.0f, 0.0f});
    return Load(name.data(), m, doodadSet);
}

// CreateEntityMapObjDef's placement (0x006953E0): translated, then turned
// about z.
void Placement(const float (&position)[3], float yaw, float (&out)[16]) {
    Identity(out);
    Translate(out, position);
    Rotate(out, yaw, {0.0f, 0.0f, 1.0f});
}

void Free(Backdrop *backdrop) {
    if (backdrop == nullptr)
        return;
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_CMAPOBJ_COUNT_DOWN)(backdrop->object);
    delete backdrop;
}

void Draw(Backdrop &b, void *frame, const float (&eye)[3], float farClip) {
    void *object = b.object;
    if (Game::Read<uint8_t>(object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return;
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    bool any = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (void *group = Group(object, i))
            any = KeepGroup(object, i, group) || any;
    }
    if (!any)
        return;
    alignas(16) uint8_t volume[Offsets::FRUSTUM_VOLUME_SIZE] = {};
    BuildViewVolume(volume, eye);
    Matrix toEye, world;
    Identity(toEye);
    Translate(toEye, {-eye[0], -eye[1], -eye[2]});
    Multiply(world, b.placement, toEye);

    using Fn_t = void(__cdecl *)();
    using Unit_t = void(__fastcall *)(int unit);
    using Load_t = void(__fastcall *)(int unit, const float *m);
    constexpr int kWorldStack = 8;
    reinterpret_cast<Fn_t>(Offsets::FUN_GX_STATE_PUSH)();
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_PUSH)(kWorldStack);
    reinterpret_cast<Load_t>(Offsets::FUN_GX_TEXMTX_LOAD)(kWorldStack, world);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_PUSH)();
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_FRUSTUM_STORE)(volume);
    reinterpret_cast<void(__fastcall *)(const float *)>(Offsets::FUN_FRUSTUM_TRANSFORM)(b.inverse);
    CommitLighting(b, frame, eye, farClip);
    DrawGroups(b, frame);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_POP)();
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_POP)(kWorldStack);
    reinterpret_cast<Fn_t>(Offsets::FUN_GX_STATE_POP)();
}

} // namespace Model::Scene::World
