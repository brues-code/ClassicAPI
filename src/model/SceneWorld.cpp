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
//
// Doodads: the world makes a group's doodads when the group is parsed
// (CreateGroupDoodads 0x00695AA0), and so does the scene's update, as M2
// models in the frame's own scene. Each is the world's def for the scene:
// the MODD record's translate, quaternion and scale times the placement, the
// model from its MODN name, the world's done callback, the default sequence
// (CreateGroupDoodadDef 0x00694E90, CreateDoodadModel 0x00695100). Its light
// is CMapDoodadDef::AddLights (0x006A8050): a doodad of an interior group
// takes its MODD colour split into an ambient (cap 0x60) and a directional
// (floor 0x70) along the world's fixed interior direction, one of an
// exterior group the sun (the scene light, by the frame's callback, with the
// scene fog), and each takes its groups' MOLT lights as AddMapLight
// (0x006A7AC0) turns them: a directional from the light, faded from the
// light's start to its end. The fog of an interior doodad is the WMO's own
// when the WMO has more than one fog (the world gives it the camera WMO's
// fog), else the scene's (0x006C31E0). A doodad two groups list is made once
// and takes both groups' lights; an exterior group makes it sun-lit. Torch
// models emit their own M2 lights into the scene's light grid by being
// animated, so the actors and the WMO get them as the world's do. Not
// mirrored: the light gate past the fog end (0x00672A20; past the end the fog
// colour is all that draws) and the water clip slot (0x006A7650).
//
// Sounds, a switch of the scene (SetPlayWorldModelSounds): the doodad event
// callback's $DSO / $DSL / $DSE (DoodadModelEvent 0x006951E0) through the
// world's own one-shot play and loop emitters, so each loop's cutoff, its
// one-play-per-id rule and the four-loop cap are the engine's, each point
// heard from the scene camera (Sounds::Heard, model/SceneSound.cpp). The
// emitters' pump runs only in the world, so a
// scene at the glue screen plays none; a hidden scene drops its loops, and
// the next $DSL of the model brings each back.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "gx/Device.h"
#include "model/M2.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Model::Scene::World {

// An M2Light, as the lighting record takes it.
struct Light {
    alignas(4) uint8_t bytes[Offsets::M2LIGHT_SIZE];
};

// A group's MOLT light as CreateGroupLights (0x00695C00) makes it.
struct MapLight {
    Light light;
    float start, end, falloff;
};

struct Doodad {
    Backdrop *backdrop;
    void *frame;
    void *model; // one reference, or null when the file did not make one
    bool exterior;
    uint32_t ambient;     // BGRA bytes, SplitLightColor's
    uint32_t directional; // BGRA bytes
    float position[3];    // the def's +0x24: the record's position placed
    float matrix[16];     // the def's +0xCC: the record's placement, then the WMO's
    float scale;          // the record's
    // The def's sphere (+0x5C, +0x68), set once the model has loaded.
    bool bounded;
    float center[3];
    float radius;
    std::vector<Light> lights;
    uint32_t sound;        // $DSL emitter handle, or 0
    float soundPoint[3];   // its point, in scene space
};

struct Backdrop {
    void *object; // CMapObj, one reference ours
    float placement[16];
    float inverse[16];
    uint32_t doodadSet;
    std::vector<std::unique_ptr<Doodad>> doodads;
    std::vector<Doodad *> byIndex; // MODD index -> its doodad
    std::vector<bool> groupsMade;
    std::vector<std::vector<MapLight>> groupLights; // per group, once made
    uint32_t serial = 0;     // this backdrop among all loaded, for the actors' caches
    uint32_t generation = 0; // one more each time a group's doodads and lights are made
    float farClip = 0.0f;
    bool sounds = false; // SetPlayWorldModelSounds, in the world
};

// An actor's place in the WMO (Settle), as the world keeps it on the unit's
// map entity.
struct Ambience {
    uint32_t serial = 0; // the backdrop's, 0 before the first placement
    const void *model = nullptr;
    uint32_t generation = 0;
    float position[3] = {};
    bool interior = false;  // baked from a face; else the sun
    uint32_t ambient = 0;   // BGRA bytes (+0x9C)
    uint32_t directional = 0; // BGRA bytes (+0xA0)
    uint32_t dayWeight = 0; // a day face's blend toward the sun (+0xA3)
    std::vector<Light> lights;
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

bool IsParsed(const void *group) {
    return group != nullptr && Game::Read<uint8_t>(group, Offsets::OFF_CMAPOBJGROUP_PARSED) != 0;
}

bool IsExterior(const void *group) {
    return (Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_FLAGS) & Offsets::MAPOBJ_GROUP_EXTERIOR_MASK) != 0;
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

using SetFog_t = void(__thiscall *)(void *record, const float *rgb, float start, float end);

void AddLight(void *record, const void *light) {
    reinterpret_cast<void(__thiscall *)(void *, const void *)>(Offsets::FUN_M2LIGHTING_ADD_LIGHT)(record, light);
}

// The frame's light callback: the scene fog, then the scene light.
void FrameLightAndFog(void *record, void *frame) {
    using Callback_t = void(__fastcall *)(void *instance, void *record, void *frame);
    reinterpret_cast<Callback_t>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB)(nullptr, record, frame);
}

// The callback's fog half alone (0x0076D68A..0x0076D7C1).
void SceneFog(void *record, void *frame) {
    if ((Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) & Offsets::SIMPLEMODEL_FOG_ARMED) == 0)
        return;
    float rgb[3];
    reinterpret_cast<void(__thiscall *)(const void *, float *)>(Offsets::FUN_UNPACK_COLOR_BYTES)(
        Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR), rgb);
    reinterpret_cast<SetFog_t>(Offsets::FUN_M2LIGHTING_SET_FOG)(
        record, rgb, Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR),
        Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR));
}

bool HasOwnFog(const Backdrop &b) { return Game::Read<uint32_t>(b.object, Offsets::OFF_CMAPOBJ_FOG_COUNT) > 1; }

// A WMO with its own fog: its record 0, the end capped at the far clip.
void WmoFog(const Backdrop &b, void *record, float farClip) {
    const uint8_t *fog = Game::Read<const uint8_t *>(b.object, Offsets::OFF_CMAPOBJ_FOGS);
    float end = Game::Read<float>(fog, Offsets::OFF_MAPOBJ_FOG_END);
    if (!(end < farClip))
        end = farClip;
    const float start = Game::Read<float>(fog, Offsets::OFF_MAPOBJ_FOG_START_FRACTION) * end;
    const uint32_t color = Game::Read<uint32_t>(fog, Offsets::OFF_MAPOBJ_FOG_COLOR);
    const float rgb[3] = {static_cast<float>((color >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((color >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(color & 0xFF) / 255.0f};
    reinterpret_cast<SetFog_t>(Offsets::FUN_M2LIGHTING_SET_FOG)(record, rgb, start, end);
}

// The WMO's record: the scene light as the sun with the WMO's own fog, else
// the frame's callback (the scene fog and light, as a scene model gets them).
void StageLightAndFog(Backdrop &b, void *record, void *frame, float farClip) {
    if (!HasOwnFog(b)) {
        FrameLightAndFog(record, frame);
        return;
    }
    AddLight(record, Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT));
    WmoFog(b, record, farClip);
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
        if (!IsParsed(group))
            continue;
        if (Game::Read<uint8_t>(group, Offsets::OFF_CMAPOBJGROUP_VERTEX_FADE_DONE) == 0)
            reinterpret_cast<Fix_t>(Offsets::FUN_CMAPOBJGROUP_FIX_VERTEX_ALPHA)(object, group);
        reinterpret_cast<Draw_t>(IsExterior(group) ? Offsets::FUN_MAPOBJ_DRAW_EXTERIOR_GROUP
                                                   : Offsets::FUN_MAPOBJ_DRAW_INTERIOR_GROUP)(object, group, 0);
    }
    sun = savedSun;
    fogFlag = savedFogFlag;
}

// ---- doodads --------------------------------------------------------------------------

float EngineFloat(uintptr_t address) { return Game::Read<float>(address); }

void TransformPoint(const float (&in)[3], const Matrix &m, float (&out)[3]) {
    float v[3] = {in[0], in[1], in[2]};
    reinterpret_cast<float *(__fastcall *)(float *, float *, const float *)>(Offsets::FUN_TRANSFORM_POINT_IN_PLACE)(
        out, v, m);
}

void ReadVector(const uint8_t *at, float (&out)[3]) { std::memcpy(out, at, sizeof out); }

// BGRA bytes as the rgb the record adds (byte / 255).
void ColorOf(uint32_t bgra, float (&rgb)[3]) {
    const float k = EngineFloat(Offsets::VAR_ONE_OVER_255);
    rgb[0] = static_cast<float>((bgra >> 16) & 0xFF) * k;
    rgb[1] = static_cast<float>((bgra >> 8) & 0xFF) * k;
    rgb[2] = static_cast<float>(bgra & 0xFF) * k;
}

using LightInt_t = void(__thiscall *)(void *light, int value);
using LightVector_t = void(__thiscall *)(void *light, const float *v);

MapLight MakeMapLight(const Backdrop &b, const uint8_t *record) {
    MapLight m{};
    void *light = m.light.bytes;
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_M2LIGHT_INITIALIZE)(light);
    float position[3];
    ReadVector(record + Offsets::OFF_MOLT_POSITION, position);
    TransformPoint(position, b.placement, position);
    m.start = Game::Read<float>(record, Offsets::OFF_MOLT_START);
    m.end = Game::Read<float>(record, Offsets::OFF_MOLT_END);
    if (std::fabs(m.start - m.end) < EngineFloat(Offsets::VAR_MAP_LIGHT_RANGE_EPSILON))
        m.end += EngineFloat(Offsets::VAR_MAP_LIGHT_RANGE_PAD);
    m.falloff = EngineFloat(Offsets::VAR_ONE) / (m.end - m.start);
    reinterpret_cast<LightInt_t>(Offsets::FUN_M2LIGHT_SET_ENABLED)(light, 1);
    reinterpret_cast<LightInt_t>(Offsets::FUN_M2LIGHT_SET_TYPE)(light, 1);
    reinterpret_cast<LightVector_t>(Offsets::FUN_M2LIGHT_SET_POSITION)(light, position);
    const float intensity = Game::Read<float>(record, Offsets::OFF_MOLT_INTENSITY);
    float rgb[3];
    ColorOf(Game::Read<uint32_t>(record, Offsets::OFF_MOLT_COLOR), rgb);
    for (float &c : rgb)
        c *= intensity;
    if (record[Offsets::OFF_MOLT_USE_DIFFUSE] != 0)
        std::memcpy(m.light.bytes + Offsets::OFF_M2LIGHT_DIFFUSE, rgb, sizeof rgb);
    if (record[Offsets::OFF_MOLT_USE_SPECULAR] != 0)
        std::memcpy(m.light.bytes + Offsets::OFF_M2LIGHT_SPECULAR, rgb, sizeof rgb);
    return m;
}

// AddMapLight (0x006A7AC0) for a def's or a unit's position: the light as a
// directional from it, faded from its start to its end.
void AddMapLight(const float (&position)[3], const MapLight &m, std::vector<Light> &lights) {
    if (m.falloff == 0.0f) {
        lights.push_back(m.light);
        return;
    }
    float lightPosition[3];
    std::memcpy(lightPosition, m.light.bytes + Offsets::OFF_M2LIGHT_POSITION, sizeof lightPosition);
    const float offset[3] = {position[0] - lightPosition[0], position[1] - lightPosition[1],
                             position[2] + EngineFloat(Offsets::VAR_MAP_LIGHT_LIFT) - lightPosition[2]};
    const float distance = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
    float intensity = 1.0f;
    if (!(distance < m.start)) {
        if (!(distance < m.end))
            return;
        intensity = EngineFloat(Offsets::VAR_ONE) - (distance - m.start) * m.falloff;
    }
    Light added = m.light;
    void *light = added.bytes;
    const float k = EngineFloat(Offsets::VAR_ONE) / distance;
    const float direction[3] = {k * offset[0], k * offset[1], k * offset[2]};
    reinterpret_cast<LightVector_t>(Offsets::FUN_M2LIGHT_SET_DIRECTION)(light, direction);
    float *diffuse = reinterpret_cast<float *>(added.bytes + Offsets::OFF_M2LIGHT_DIFFUSE);
    for (int i = 0; i < 3; ++i)
        diffuse[i] *= intensity;
    reinterpret_cast<LightInt_t>(Offsets::FUN_M2LIGHT_SET_ENABLED)(light, 1);
    reinterpret_cast<LightInt_t>(Offsets::FUN_M2LIGHT_SET_TYPE)(light, 0);
    const float attenuation[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(added.bytes + Offsets::OFF_M2LIGHT_ATTENUATION, attenuation, sizeof attenuation);
    lights.push_back(added);
}

const float (&MapLightDirection())[3] {
    return *reinterpret_cast<const float(*)[3]>(static_cast<uintptr_t>(Offsets::VAR_MAP_LIGHT_DIRECTION));
}

// The interior slots of a def or a unit: the WMO fog (the scene's when the
// WMO has fewer than two), the ambient bytes and the directional bytes along
// `direction`.
void InteriorLight(const Backdrop &b, void *record, void *frame, uint32_t ambientBytes, uint32_t directionalBytes,
                   const float (&direction)[3]) {
    if (HasOwnFog(b))
        WmoFog(b, record, b.farClip);
    else
        SceneFog(record, frame);
    float ambient[3], directional[3];
    ColorOf(ambientBytes, ambient);
    ColorOf(directionalBytes, directional);
    reinterpret_cast<void(__thiscall *)(void *, const float *)>(Offsets::FUN_M2LIGHTING_ADD_AMBIENT)(record, ambient);
    reinterpret_cast<void(__thiscall *)(void *, const float *, const float *)>(Offsets::FUN_M2LIGHTING_ADD_DIRECTIONAL)(
        record, directional, direction);
}

// The model's lighting callback, `__fastcall(instance, record, user)` ret 4,
// as the world's ApplyEntityLighting (0x00672A20) runs a def's fog and light
// slots.
void __fastcall DoodadLighting(void * /*instance*/, void *record, Doodad *d) {
    const Backdrop &b = *d->backdrop;
    if (d->exterior)
        FrameLightAndFog(record, d->frame);
    else
        InteriorLight(b, record, d->frame, d->ambient, d->directional, MapLightDirection());
    for (const Light &light : d->lights)
        AddLight(record, light.bytes);
}

// ---- sounds ---------------------------------------------------------------------------

void DropSound(Doodad &d) {
    if (d.sound == 0)
        return;
    reinterpret_cast<void(__fastcall *)(uint32_t)>(Offsets::FUN_SOUND_REMOVE_EMITTER)(d.sound);
    d.sound = 0;
}

void MoveSound(Doodad &d) {
    float point[3];
    Sounds::Heard(d.frame, d.soundPoint, point);
    reinterpret_cast<void(__fastcall *)(uint32_t, const float *)>(Offsets::FUN_SOUND_MOVE_EMITTER)(d.sound, point);
}

constexpr uint32_t Event(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}
constexpr uint32_t kSoundOnce = Event('$', 'D', 'S', 'O');
constexpr uint32_t kSoundLoop = Event('$', 'D', 'S', 'L');
constexpr uint32_t kSoundEnd = Event('$', 'D', 'S', 'E');
static_assert(kSoundEnd == 0x45534424 && kSoundLoop == 0x4C534424 && kSoundOnce == 0x4F534424,
              "the codes DoodadModelEvent compares");

// DoodadModelEvent (0x006951E0) with the point heard from the scene camera.
void __stdcall OnDoodadEvent(uint32_t tag, uint32_t soundID, const float *point, uint32_t /*unknown*/,
                             uint32_t userLow, uint32_t /*userHigh*/) {
    auto &d = *reinterpret_cast<Doodad *>(static_cast<uintptr_t>(userLow));
    const Backdrop &b = *d.backdrop;
    if (!b.sounds)
        return;
    if (tag == kSoundEnd) {
        DropSound(d);
        return;
    }
    if ((tag != kSoundLoop && tag != kSoundOnce) || point == nullptr)
        return;
    const float scenePoint[3] = {point[0], point[1], point[2]};
    float heard[3];
    Sounds::Heard(d.frame, scenePoint, heard);
    if (tag == kSoundOnce) {
        using PlayAt_t = void(__fastcall *)(uint32_t id, const float *pos, int index, float volume);
        reinterpret_cast<PlayAt_t>(Offsets::FUN_SOUND_PLAY_AT)(soundID, heard, -1, 1.0f);
        return;
    }
    if (d.sound != 0 &&
        reinterpret_cast<uint32_t(__fastcall *)(uint32_t)>(Offsets::FUN_SOUND_EMITTER_SOUND_ID)(d.sound) != soundID)
        DropSound(d);
    std::memcpy(d.soundPoint, scenePoint, sizeof d.soundPoint);
    if (d.sound == 0)
        d.sound = reinterpret_cast<uint32_t(__fastcall *)(uint32_t, const float *, uint8_t)>(
            Offsets::FUN_SOUND_ADD_EMITTER)(soundID, heard, 0);
    else
        reinterpret_cast<void(__fastcall *)(uint32_t, const float *)>(Offsets::FUN_SOUND_MOVE_EMITTER)(d.sound, heard);
}

// ---- making them ----------------------------------------------------------------------

// CreateGroupDoodadDef (0x00694E90) and CreateDoodadModel (0x00695100) in the
// frame's scene.
Doodad *MakeDoodad(Backdrop &b, void *frame, uint32_t index) {
    const uint8_t *record = Game::Read<const uint8_t *>(b.object, Offsets::OFF_CMAPOBJ_DOODADS) +
                            static_cast<size_t>(index) * Offsets::MODD_SIZE;
    const char *path = Game::Read<const char *>(b.object, Offsets::OFF_CMAPOBJ_DOODAD_NAMES) +
                       Game::Read<uint32_t>(record, Offsets::OFF_MODD_NAME);
    auto owned = std::make_unique<Doodad>();
    Doodad &d = *owned;
    d.backdrop = &b;
    d.frame = frame;
    float position[3];
    ReadVector(record + Offsets::OFF_MODD_POSITION, position);
    TransformPoint(position, b.placement, d.position);
    Matrix m;
    Identity(m);
    Translate(m, position);
    reinterpret_cast<void(__thiscall *)(float *, const float *)>(Offsets::FUN_MATRIX_ROTATE_QUATERNION)(
        m, Game::Ptr<float>(record, Offsets::OFF_MODD_ROTATION));
    reinterpret_cast<void(__thiscall *)(float *, float)>(Offsets::FUN_MATRIX_SCALE)(
        m, Game::Read<float>(record, Offsets::OFF_MODD_SCALE));
    reinterpret_cast<float *(__thiscall *)(float *, const float *)>(Offsets::FUN_MATRIX_MULTIPLY_IN_PLACE)(
        m, b.placement);
    std::memcpy(d.matrix, m, sizeof d.matrix);
    d.scale = Game::Read<float>(record, Offsets::OFF_MODD_SCALE);
    using Split_t = void(__stdcall *)(const uint32_t *src, uint32_t *first, uint8_t floor, uint32_t *second,
                                      uint8_t cap);
    reinterpret_cast<Split_t>(Offsets::FUN_SPLIT_LIGHT_COLOR)(Game::Ptr<uint32_t>(record, Offsets::OFF_MODD_COLOR),
                                                             &d.directional, Offsets::DOODAD_DIRECTIONAL_FLOOR,
                                                             &d.ambient, Offsets::DOODAD_AMBIENT_CAP);
    d.model = M2::CreateModel(Model::Pane::Scene(frame), path);
    if (d.model != nullptr) {
        reinterpret_cast<void(__thiscall *)(void *, const float *)>(Offsets::FUN_M2MODEL_SET_BASE_TRANSFORM)(d.model,
                                                                                                          m);
        const auto user = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&d));
        M2::SetAnimDoneCallback(d.model, reinterpret_cast<const void *>(Offsets::FUN_DOODAD_MODEL_DONE), 0, 0);
        M2::SetEventCallback(d.model, reinterpret_cast<const void *>(&OnDoodadEvent), user, 0);
        M2::SetRenderCallback(d.model, reinterpret_cast<const void *>(&DoodadLighting), &d);
        M2::PlayAnimation(d.model, -1, 0, -1, 0, 1.0f, 1, 1);
    }
    b.byIndex[index] = &d;
    b.doodads.push_back(std::move(owned));
    return &d;
}

// CreateGroupDoodads (0x00695AA0) with the group's lights (0x00695C00) for
// each doodad it holds.
void MakeGroupDoodads(Backdrop &b, void *frame, uint32_t groupIndex, void *group) {
    const auto *molt = Game::Read<const uint8_t *>(b.object, Offsets::OFF_CMAPOBJ_LIGHTS);
    const uint32_t moltCount = Game::Read<uint32_t>(b.object, Offsets::OFF_CMAPOBJ_LIGHT_COUNT);
    const auto *lightRefs = Game::Read<const uint16_t *>(group, Offsets::OFF_CMAPOBJGROUP_LIGHT_REFS);
    std::vector<MapLight> lights;
    for (uint32_t i = 0, n = Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_LIGHT_REF_COUNT); i < n; ++i) {
        const uint32_t ref = lightRefs[i];
        const uint8_t *record = molt + static_cast<size_t>(ref) * Offsets::MOLT_SIZE;
        if (ref < moltCount && record[0] == 0)
            lights.push_back(MakeMapLight(b, record));
    }
    using FindSet_t = uint32_t(__thiscall *)(void *object, uint32_t index);
    const bool exterior = IsExterior(group);
    const auto *doodadRefs = Game::Read<const uint16_t *>(group, Offsets::OFF_CMAPOBJGROUP_DOODAD_REFS);
    for (uint32_t i = 0, n = Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_DOODAD_REF_COUNT); i < n; ++i) {
        const uint32_t index = doodadRefs[i];
        const uint32_t set = reinterpret_cast<FindSet_t>(Offsets::FUN_CMAPOBJ_FIND_DOODAD_SET)(b.object, index);
        if (set == Offsets::MAPOBJ_DOODAD_SET_NONE || (set != 0 && set != b.doodadSet) || index >= b.byIndex.size())
            continue;
        Doodad *d = b.byIndex[index] != nullptr ? b.byIndex[index] : MakeDoodad(b, frame, index);
        d->exterior = d->exterior || exterior;
        for (const MapLight &light : lights)
            AddMapLight(d->position, light, d->lights);
    }
    b.groupLights[groupIndex] = std::move(lights);
    ++b.generation;
}

// SetDoodadDefBounds (0x006952A0), for a loaded model.
void Bound(Doodad &d) {
    float sphere[4];
    M2::GetBoundingSphere(d.model, sphere);
    const float centre[3] = {sphere[0], sphere[1], sphere[2]};
    TransformPoint(centre, d.matrix, d.center);
    d.radius = sphere[3] * d.scale;
    d.bounded = true;
}

// DrawDoodadModels' fade (0x00683F80): by the doodad's size and the camera's
// ground distance to it; drawn only while > 0.
float FadeAlpha(const Doodad &d, const float (&eye)[3], float farClip) {
    if (d.radius > EngineFloat(Offsets::VAR_DOODAD_FADE_RADIUS_LARGE))
        return 1.0f;
    float range, end;
    if (d.radius <= EngineFloat(Offsets::VAR_DOODAD_FADE_RADIUS_SMALL)) {
        range = EngineFloat(Offsets::VAR_DOODAD_FADE_RANGE_SMALL);
        end = EngineFloat(Offsets::VAR_DOODAD_FADE_END_SMALL);
    } else if (d.radius <= EngineFloat(Offsets::VAR_DOODAD_FADE_RADIUS_MEDIUM)) {
        range = EngineFloat(Offsets::VAR_DOODAD_FADE_RANGE_MEDIUM);
        end = EngineFloat(Offsets::VAR_DOODAD_FADE_END_MEDIUM);
    } else {
        range = EngineFloat(Offsets::VAR_DOODAD_FADE_RANGE_LARGE);
        end = EngineFloat(Offsets::VAR_DOODAD_FADE_END_LARGE);
        if (farClip < end)
            end = farClip;
    }
    const float dx = d.center[0] - eye[0], dy = d.center[1] - eye[1];
    const float distance = std::sqrt(dx * dx + dy * dy) - d.radius;
    const float alpha = 1.0f - (distance - (end - range)) / range;
    return alpha > 1.0f ? 1.0f : alpha;
}

void ReleaseDoodad(Doodad &d) {
    DropSound(d);
    if (d.model == nullptr)
        return;
    M2::SetAnimDoneCallback(d.model, nullptr, 0, 0);
    M2::SetEventCallback(d.model, nullptr, 0, 0);
    M2::SetRenderCallback(d.model, nullptr, nullptr);
    M2::SetVisible(d.model, false);
    M2::Release(d.model);
    d.model = nullptr;
}

} // namespace

bool IsWmoPath(const char *path) { return path != nullptr && HasWmoSuffix(path); }

Backdrop *Load(const char *path, const float (&placement)[16], uint32_t doodadSet) {
    if (!IsWmoPath(path) || CheckedGroupCount(path) < 0)
        return nullptr;
    void *object = reinterpret_cast<void *(__fastcall *)(const char *)>(Offsets::FUN_CMAPOBJ_GET)(path);
    if (object == nullptr)
        return nullptr;
    static uint32_t serials = 0;
    auto *b = new Backdrop{object, {}, {}, doodadSet};
    b->serial = ++serials;
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
    for (const auto &d : backdrop->doodads)
        ReleaseDoodad(*d);
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_CMAPOBJ_COUNT_DOWN)(backdrop->object);
    delete backdrop;
}

void Silence(Backdrop &b) {
    for (const auto &d : b.doodads)
        DropSound(*d);
}

// ---- actors ---------------------------------------------------------------------------

namespace {

void TransformBox(const float *m, const float (&box)[6], float (&out)[6]) {
    reinterpret_cast<void(__fastcall *)(const float *, const float *, float *)>(Offsets::FUN_TRANSFORM_BOX)(m, box,
                                                                                                           out);
}

// The sun's colour as the bytes a day face blends toward: the scene light's.
uint32_t SunBytes(const uint8_t *sun, uintptr_t field) {
    const auto *rgb = reinterpret_cast<const float *>(sun + field);
    const auto byte = [](float c) {
        const float v = c * 255.0f;
        return static_cast<uint32_t>(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
    };
    return 0xFF000000u | byte(rgb[0]) << 16 | byte(rgb[1]) << 8 | byte(rgb[2]);
}

// BakeGroupLight's day blend: each channel moves (to - from) x weight / 256
// toward `to`, in the engine's unsigned byte arithmetic; 0xFF takes `to`.
uint32_t BlendBytes(uint32_t from, uint32_t to, uint32_t weight) {
    if (weight == 0xFF)
        return (from & 0xFF000000u) | (to & 0x00FFFFFFu);
    uint32_t out = from & 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t f = (from >> shift) & 0xFF, t = (to >> shift) & 0xFF;
        out |= static_cast<uint32_t>(static_cast<uint8_t>(f + (((t - f) * weight) >> 8))) << shift;
    }
    return out;
}

// LinkEntityToMapObject (0x006A8ED0) for the one WMO, then SetEntityLightFlags
// (0x006A87F0) and BakeGroupLight's face path (0x006A8410).
void Place(const Backdrop &b, Ambience &a, void *frame, void *model, const float *base) {
    using Overlaps_t = bool(__thiscall *)(void *object, const float *box);
    using OverlapsGroup_t = bool(__thiscall *)(void *object, const float *box, uint32_t group, int);
    using GetGroup_t = void *(__thiscall *)(void *object, uint32_t group, int force);
    using BoxHit_t = bool(__thiscall *)(void *object, const float *box, uint32_t group);
    using SegmentHit_t = bool(__thiscall *)(void *object, const float *start, const float *end, float *t,
                                            uint32_t group);
    using Trace_t = bool(__thiscall *)(void *object, const float *start, const float *end, uint32_t group,
                                       uint32_t mask, uint16_t *face, float *t);
    using Sample_t = bool(__thiscall *)(void *object, uint32_t group, const float *point, uint16_t face,
                                        uint32_t *bgra, bool *day);
    using Split_t = void(__stdcall *)(const uint32_t *src, uint32_t *first, uint8_t floor, uint32_t *second,
                                      uint8_t cap);
    void *object = b.object;
    a.lights.clear();
    a.interior = false;

    // SyncLightNode's world box and centre: the model's bounds through its
    // matrix, or its translation when the bounds are empty.
    float box[6];
    M2::GetBoundingBox(model, box);
    float world[6], centre[3];
    if (box[0] > box[3] || box[1] > box[4] || box[2] > box[5]) {
        for (int i = 0; i < 3; ++i)
            world[i] = world[i + 3] = centre[i] = a.position[i];
    } else {
        TransformBox(base, box, world);
        for (int i = 0; i < 3; ++i)
            centre[i] = (world[i] + world[i + 3]) * 0.5f;
    }
    float local[6];
    TransformBox(b.inverse, world, local);
    if (!reinterpret_cast<Overlaps_t>(Offsets::FUN_CMAPOBJ_BOX_OVERLAPS)(object, local))
        return;
    const float length = EngineFloat(Offsets::VAR_ENTITY_PROBE_LENGTH);
    const float below[3] = {centre[0], centre[1], centre[2] - length};
    float start[3], end[3];
    TransformPoint(centre, b.inverse, start);
    TransformPoint(below, b.inverse, end);
    const float up[3] = {start[0], start[1], start[2] + length};

    float t = 1.0f;
    uint16_t face = 0;
    bool hit = false;
    uint32_t hitGroup = 0;
    float point[3] = {};
    const auto trace = reinterpret_cast<Trace_t>(Offsets::FUN_CMAPOBJ_TRACE_GROUP_MASKED);
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    for (uint32_t g = 0; g < count; ++g) {
        if (!reinterpret_cast<OverlapsGroup_t>(Offsets::FUN_CMAPOBJ_BOX_OVERLAPS_GROUP)(object, local, g, 1))
            continue;
        void *group = reinterpret_cast<GetGroup_t>(Offsets::FUN_CMAPOBJ_GET_LOADED_GROUP)(object, g, 0);
        if (group == nullptr)
            continue;
        if (!reinterpret_cast<BoxHit_t>(Offsets::FUN_CMAPOBJ_GROUP_BOX_ANY_HIT)(object, local, g) &&
            !reinterpret_cast<SegmentHit_t>(Offsets::FUN_CMAPOBJ_GROUP_SEGMENT_ANY_HIT)(object, start, end, &t, g))
            continue;
        const uint32_t flags = Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_FLAGS);
        if ((flags & Offsets::MAPOBJ_GROUP_EXTERIOR) == 0 &&
            (trace(object, start, end, g, Offsets::MAPOBJ_TRACE_MASK_ENTITY, &face, &t) ||
             trace(object, start, up, g, Offsets::MAPOBJ_TRACE_MASK_ENTITY, &face, &t))) {
            hit = true;
            hitGroup = g;
            for (int i = 0; i < 3; ++i)
                point[i] = centre[i] + (below[i] - centre[i]) * t;
        }
        // The group links the unit: its lights light it (GatherMapObjectLights).
        if (g < b.groupLights.size())
            for (const MapLight &light : b.groupLights[g])
                AddMapLight(a.position, light, a.lights);
    }
    if (!hit)
        return;
    void *group = reinterpret_cast<GetGroup_t>(Offsets::FUN_CMAPOBJ_GET_LOADED_GROUP)(object, hitGroup, 0);
    if ((Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_FLAGS) & Offsets::MAPOBJ_GROUP_EXTERIOR_LIT) != 0)
        return;
    a.interior = true;
    float sample[3];
    TransformPoint(point, b.inverse, sample);
    uint32_t color = 0;
    bool day = false;
    if (!reinterpret_cast<Sample_t>(Offsets::FUN_CMAPOBJ_SAMPLE_GROUP_COLOR)(object, hitGroup, sample, face, &color,
                                                                            &day))
        return; // the bytes stay as they were, as on the unit
    reinterpret_cast<Split_t>(Offsets::FUN_SPLIT_LIGHT_COLOR)(&color, &a.directional,
                                                             Offsets::ENTITY_DIRECTIONAL_FLOOR, &a.ambient,
                                                             Offsets::DOODAD_AMBIENT_CAP);
    a.dayWeight = 0;
    if (!day)
        return;
    const auto *sun = Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT);
    const uint32_t weight = color >> 24;
    if (weight != 0) {
        a.directional = BlendBytes(a.directional, SunBytes(sun, Offsets::OFF_M2LIGHT_DIFFUSE), weight);
        a.ambient = BlendBytes(a.ambient, SunBytes(sun, Offsets::OFF_M2LIGHT_AMBIENT), weight);
    }
    a.dayWeight = weight;
}

} // namespace

Ambience *NewAmbience() { return new Ambience(); }

void FreeAmbience(Ambience *ambience) { delete ambience; }

void Settle(const Backdrop &b, Ambience &a, void *frame, void *model) {
    if (Game::Read<uint8_t>(b.object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return;
    const float *base = Game::Ptr<float>(model, Offsets::OFF_M2MODEL_BASE_MATRIX);
    const float position[3] = {base[12], base[13], base[14]};
    if (a.serial == b.serial && a.model == model && a.generation == b.generation) {
        const float dx = position[0] - a.position[0], dy = position[1] - a.position[1],
                    dz = position[2] - a.position[2];
        if (std::fabs(dx * dx + dy * dy + dz * dz) < EngineFloat(Offsets::VAR_MAP_LIGHT_RANGE_EPSILON))
            return;
    }
    a.serial = b.serial;
    a.model = model;
    a.generation = b.generation;
    std::memcpy(a.position, position, sizeof a.position);
    Place(b, a, frame, model, base);
}

// ApplyMapLighting (0x006A7300).
void ActorLight(const Backdrop *b, const Ambience &a, void *frame, void *record) {
    if (b == nullptr || a.serial != b->serial) {
        FrameLightAndFog(record, frame);
        return;
    }
    if (!a.interior) {
        FrameLightAndFog(record, frame);
    } else {
        float direction[3] = {MapLightDirection()[0], MapLightDirection()[1], MapLightDirection()[2]};
        if (a.dayWeight != 0) {
            const auto *sun = reinterpret_cast<const float *>(
                Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT) + Offsets::OFF_M2LIGHT_DIRECTION);
            const float f = static_cast<float>(a.dayWeight) * EngineFloat(Offsets::VAR_ONE_OVER_255);
            for (int i = 0; i < 3; ++i)
                direction[i] += (sun[i] - direction[i]) * f;
            const float k =
                EngineFloat(Offsets::VAR_ONE) /
                std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
            for (float &c : direction)
                c *= k;
        }
        InteriorLight(*b, record, frame, a.ambient, a.directional, direction);
    }
    for (const Light &light : a.lights)
        AddLight(record, light.bytes);
}

void Update(Backdrop &b, void *frame, bool sounds) {
    const View view = CameraView(frame);
    b.farClip = view.farClip;
    b.sounds = sounds && Game::Read<uint8_t>(Offsets::VAR_IN_WORLD) != 0;
    if (!b.sounds)
        Silence(b);
    void *object = b.object;
    if (Game::Read<uint8_t>(object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return;
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    if (b.groupsMade.empty()) {
        b.groupsMade.assign(count, false);
        b.groupLights.assign(count, {});
        b.byIndex.assign(Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_DOODAD_COUNT), nullptr);
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (b.groupsMade[i])
            continue;
        void *group = Group(object, i);
        if (!IsParsed(group))
            continue;
        MakeGroupDoodads(b, frame, i, group);
        b.groupsMade[i] = true;
    }
    // The animate list empties at each paint (as for the actors' models), and
    // the world shows each doodad it draws again each frame, faded by distance
    // (DrawDoodadModels, 0x00683F80).
    for (const auto &d : b.doodads) {
        if (d->model != nullptr && M2::IsReady(d->model, 0, 1)) {
            if (!d->bounded)
                Bound(*d);
            const float alpha = FadeAlpha(*d, view.eye, view.farClip);
            const bool shown = alpha > 0.0f;
            M2::SetVisible(d->model, shown);
            if (shown) {
                M2::SetAnimated(d->model, true);
                M2::SetAlpha(d->model, alpha);
            }
        }
        if (d->sound != 0)
            MoveSound(*d);
    }
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
