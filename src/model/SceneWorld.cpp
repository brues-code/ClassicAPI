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
// Fog: the world fogs a WMO with its own MFOG records while the camera is
// over one of its interior groups, and only for a WMO with more than one
// record. Each update traces down from the scene's camera as the world
// traces from its own (ClaimCameraMapObject 0x006821F0): the floor under it,
// or the portal it crosses, names the groups that hold it. Record 0 blended
// with the nearby candidate fogs of the first of them (GetCameraInteriorFog
// 0x0069DE20) is the target, its end capped at the far clip and its start
// the record's fraction of that; the drawn fog moves from the zone's toward
// it by a weight that rises 0.25 a second while the camera is inside and
// falls as it leaves (BuildSceneFog 0x006CEE30). The zone's fog in a scene
// is the scene's own (SetFogColor and so on), staged with the scene light by
// the frame's light callback. The WMO, its interior doodads and interior
// actors take the drawn fog.
//
// Liquids: the groups with liquid (flag 0x1000) draw after the scene's
// opaque models, as the world's group liquid pass (0x00684CD0) draws after
// them, by the world's own drawer: the lava frames, the scroll, the water's
// depth ramp.
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
// light's start to its end. The fog of an interior doodad is the camera's
// drawn fog while its weight is not 0, else the scene's (0x006C31E0). A doodad two groups list is made once
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
//
// Actors stand on the WMO as units do: AttachEntityToGround (0x006A8A20, the
// path a unit's world entity takes) probes down from the model's collision
// box centre for the nearest group face, links that group (its lights light
// the actor), takes its light class, the face's terrain type (the footsteps'
// ground) and, inside, the face's baked colour. Shadows are the world's
// decal draw (0x006D7330) over the WMO's faces in the shadow's box. The
// engine's queries take a map object def; they read only its placement, its
// inverse and the object, so the backdrop keeps a block with those three.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "gx/Device.h"
#include "model/M2.h"

#include <algorithm>
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

// The {end, second float, colour 0xAARRGGBB} triple LerpFogRecord blends: an
// MFOG sub-record (the second float the start fraction) or a drawn fog (the
// second float the start).
struct FogValues {
    float end;
    float start;
    uint32_t color;
};
static_assert(sizeof(FogValues) == 12, "the engine's sub-record");

// The camera's WMO fog, as BuildSceneFog keeps it in the light state.
struct CameraFog {
    bool primed = false;
    float weight = 0.0f; // +0x7C
    FogValues target{};  // +0x8C: end capped at the far clip, start, colour
};

struct Backdrop {
    void *object; // CMapObj, one reference ours
    float placement[16];
    float inverse[16];
    uint32_t doodadSet;
    // The fields of a map object def the engine's queries read (the
    // placement, its inverse, the object), at a def's offsets, set once.
    alignas(16) uint8_t def[Offsets::MAPOBJDEF_QUERY_SIZE] = {};
    std::vector<std::unique_ptr<Doodad>> doodads;
    std::vector<Doodad *> byIndex; // MODD index -> its doodad
    std::vector<bool> groupsMade;
    std::vector<std::vector<MapLight>> groupLights; // per group, once made
    uint32_t serial = 0;     // this backdrop among all loaded, for the actors' caches
    uint32_t generation = 0; // one more each time a group's doodads and lights are made
    float farClip = 0.0f;
    bool sounds = false; // SetPlayWorldModelSounds, in the world
    CameraFog fog;
};

static_assert(Offsets::OFF_MAPOBJDEF_OBJECT + sizeof(void *) <= Offsets::MAPOBJDEF_QUERY_SIZE &&
                  Offsets::OFF_MAPOBJDEF_INVERSE + 16 * sizeof(float) <= Offsets::OFF_MAPOBJDEF_OBJECT,
              "the def block holds the three fields at their offsets");

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
    int32_t ground = -1;    // the face's TerrainType id (+0xDC), or -1
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

void LerpFog(FogValues &fog, const void *toward, float t) {
    using Lerp_t = void(__thiscall *)(void *a, const void *b, float t);
    reinterpret_cast<Lerp_t>(Offsets::FUN_LERP_FOG_RECORD)(&fog, toward, t);
}

void SetFog(void *record, const FogValues &fog) {
    const float rgb[3] = {static_cast<float>((fog.color >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((fog.color >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(fog.color & 0xFF) / 255.0f};
    reinterpret_cast<SetFog_t>(Offsets::FUN_M2LIGHTING_SET_FOG)(record, rgb, fog.start, fog.end);
}

// The camera's WMO fog has a weight: the world then draws the WMO and the
// interior entities of the claimed WMO with the drawn fog (the light state's
// +0x80; ApplyMapObjectFog 0x006C31E0), else with the zone's.
bool WmoFogShown(const Backdrop &b) { return b.fog.weight > 0.0f; }

// The drawn fog: the zone's moved toward the target by the weight. The
// scene's fog stands for the zone's; a scene without fog stands for one
// that ends at the far clip in the target's colour.
void WmoFog(const Backdrop &b, void *record, void *frame) {
    FogValues drawn{b.farClip, b.farClip, b.fog.target.color};
    if ((Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) & Offsets::SIMPLEMODEL_FOG_ARMED) != 0)
        drawn = {Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR),
                 Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR),
                 Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR)};
    LerpFog(drawn, &b.fog.target, b.fog.weight);
    SetFog(record, drawn);
}

// The WMO's record: the scene light as the sun with the drawn fog, else the
// frame's callback (the scene fog and light, as a scene model gets them).
void StageLightAndFog(const Backdrop &b, void *record, void *frame) {
    if (!WmoFogShown(b)) {
        FrameLightAndFog(record, frame);
        return;
    }
    AddLight(record, Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT));
    WmoFog(b, record, frame);
}

void CommitLighting(const Backdrop &b, void *frame, const float (&eye)[3]) {
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
    StageLightAndFog(b, record, frame);
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
    if (WmoFogShown(b))
        WmoFog(b, record, frame);
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
    std::memcpy(b->def + Offsets::OFF_MAPOBJDEF_PLACEMENT, b->placement, sizeof b->placement);
    std::memcpy(b->def + Offsets::OFF_MAPOBJDEF_INVERSE, b->inverse, sizeof b->inverse);
    std::memcpy(b->def + Offsets::OFF_MAPOBJDEF_OBJECT, &object, sizeof object);
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

using GetGroup_t = void *(__thiscall *)(void *object, uint32_t group, int force);

void *LoadedGroup(void *object, uint32_t group) {
    return reinterpret_cast<GetGroup_t>(Offsets::FUN_CMAPOBJ_GET_LOADED_GROUP)(object, group, 0);
}

// What every facet query does first (0x006A893B, 0x006D7497).
void ResetFacets() {
    Game::Ref<uint32_t>(Offsets::VAR_FACET_OVERFLOW) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_BATCH_COUNT) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_FACE_CURSOR) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_INDEX_CURSOR) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_MATRIX_COUNT) = 0;
}

const uint8_t *FacetBatch(uint32_t index) {
    return reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Offsets::VAR_FACET_BATCHES) +
                                             index * Offsets::FACET_BATCH_SIZE);
}

// TraceMapObjectsNearest (0x006A8840) over the one WMO: the nearest group
// face the segment crosses (mask 4), its group and the first face of the
// group's batch. The world first screens the segment against the instance's
// world box (0x006C3330); each group's own box test below already covers
// it, so it is left out.
bool Nearest(const Backdrop &b, const float (&start)[3], const float (&end)[3], float &t, uint32_t &group,
             uint16_t &face) {
    using HitsBox_t = bool(__thiscall *)(void *object, const float *start, const float *end, uint32_t group);
    using Intersect_t = bool(__thiscall *)(void *group, void *out, const float *segment, float *t, const void *def,
                                           uint32_t liquidMask, uint16_t faceMask);
    void *object = b.object;
    t = 1.0f;
    face = 0;
    bool found = false;
    float local[6];
    float (&ls)[3] = *reinterpret_cast<float (*)[3]>(local);
    float (&le)[3] = *reinterpret_cast<float (*)[3]>(local + 3);
    TransformPoint(start, b.inverse, ls);
    TransformPoint(end, b.inverse, le);
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    for (uint32_t g = 0; g < count; ++g) {
        if (!reinterpret_cast<HitsBox_t>(Offsets::FUN_CMAPOBJ_SEGMENT_HITS_GROUP_BOX)(object, ls, le, g))
            continue;
        void *loaded = LoadedGroup(object, g);
        if (loaded == nullptr)
            continue;
        ResetFacets();
        float segment[6];
        std::memcpy(segment, local, sizeof segment);
        float tt = t;
        uint32_t out = 0;
        if (reinterpret_cast<Intersect_t>(Offsets::FUN_CMAPOBJGROUP_INTERSECT_SEGMENT)(
                loaded, &out, segment, &tt, b.def, 0, Offsets::MAPOBJ_TRACE_MASK_GROUND) &&
            tt < t) {
            t = tt;
            group = g;
            found = true;
            if (const auto *faces = Game::Read<const uint16_t *>(FacetBatch(0), Offsets::OFF_FACET_BATCH_FACES))
                face = faces[0];
        }
    }
    return found;
}

// ---- the camera's fog -----------------------------------------------------------------

constexpr uint32_t kNoGroup = 0xFFFF;

uint32_t GroupInfoFlags(void *object, uint32_t group) {
    using Info_t = const uint32_t *(__thiscall *)(void *object, uint32_t group);
    const uint32_t *info = reinterpret_cast<Info_t>(Offsets::FUN_CMAPOBJ_GET_GROUP_INFO)(object, group);
    return info != nullptr ? *info : 0;
}

// ClaimCameraMapObject (0x006821F0) over the one WMO (a scene has no
// terrain): IntersectMapObjects (0x006BE250) down from the eye, the floor
// first, then the portal the segment crosses. The groups that hold the
// camera (one or two), or none when the result is an exterior group.
uint32_t ClaimCamera(const Backdrop &b, const float (&eye)[3], uint32_t (&groups)[2]) {
    using Cross_t = bool(__thiscall *)(void *object, const float *segment, float *fraction, uint32_t *groups);
    void *object = b.object;
    const float end[3] = {eye[0], eye[1], eye[2] - EngineFloat(Offsets::VAR_CAMERA_DOWN_RAY_LENGTH)};
    float t = 1.0f;
    uint32_t group = kNoGroup, other = kNoGroup;
    uint16_t face = 0;
    bool claimed = Nearest(b, eye, end, t, group, face);
    float segment[6];
    TransformPoint(eye, b.inverse, *reinterpret_cast<float (*)[3]>(segment));
    TransformPoint(end, b.inverse, *reinterpret_cast<float (*)[3]>(segment + 3));
    float fraction;
    const uint32_t bits = Offsets::CAMERA_PORTAL_FRACTION_BITS;
    std::memcpy(&fraction, &bits, sizeof fraction);
    uint32_t sides[2] = {kNoGroup, kNoGroup};
    if (reinterpret_cast<Cross_t>(Offsets::FUN_CMAPOBJ_CROSS_PORTALS)(object, segment, &fraction, sides) &&
        fraction - t < EngineFloat(Offsets::VAR_CAMERA_PORTAL_EPSILON)) {
        claimed = true;
        group = sides[0];
        other = (GroupInfoFlags(object, sides[1]) & Offsets::MOGI_FLAG_EXTERIOR) == 0 ? sides[1] : kNoGroup;
    }
    if (!claimed || (GroupInfoFlags(object, group) & Offsets::MOGI_FLAG_EXTERIOR) != 0)
        return 0;
    groups[0] = group;
    if (other == kNoGroup || other == group)
        return 1;
    groups[1] = other;
    return 2;
}

// GetCameraInteriorFog (0x0069DE20) for the claim: the dry sub-record of
// record 0 blended with the first group's candidate fogs, farthest first
// (its heap's pop order), and whether a group of the claim is interior. The
// underwater sub-record and the flags it also blends are left out: the
// camera's liquid is not asked.
// The engine reads a fog index past the records without a check; one is
// skipped here.
bool InteriorFog(const Backdrop &b, const float (&eye)[3], const uint32_t (&groups)[2], uint32_t count,
                 FogValues &dry) {
    using Falloff_t = float(__thiscall *)(const void *fog, float distance);
    void *object = b.object;
    bool inside = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (const void *g = LoadedGroup(object, groups[i]))
            inside = inside || (Game::Read<uint32_t>(g, Offsets::OFF_CMAPOBJGROUP_FLAGS) &
                                Offsets::MAPOBJ_GROUP_EXTERIOR_MASK) == 0;
    }
    const uint8_t *fogs = Game::Read<const uint8_t *>(object, Offsets::OFF_CMAPOBJ_FOGS);
    const uint32_t fogCount = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_FOG_COUNT);
    FogValues out;
    std::memcpy(&out, fogs + Offsets::OFF_MAPOBJ_FOG_END, sizeof out);
    float camera[3];
    TransformPoint(eye, b.inverse, camera);
    struct Candidate {
        float distance;
        const uint8_t *fog;
    };
    Candidate candidates[Offsets::MAPOBJ_GROUP_FOG_INDICES];
    uint32_t n = 0;
    if (const uint8_t *first = static_cast<const uint8_t *>(LoadedGroup(object, groups[0]))) {
        for (uint32_t k = 0; k < Offsets::MAPOBJ_GROUP_FOG_INDICES; ++k) {
            const uint8_t index = first[Offsets::OFF_CMAPOBJGROUP_FOG_INDICES + k];
            if (index == 0 || index >= fogCount)
                continue;
            const uint8_t *fog = fogs + index * Offsets::MAPOBJ_FOG_SIZE;
            const float *at = reinterpret_cast<const float *>(fog + Offsets::OFF_MAPOBJ_FOG_POSITION);
            const float dx = at[0] - camera[0], dy = at[1] - camera[1], dz = at[2] - camera[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance < Game::Read<float>(fog, Offsets::OFF_MAPOBJ_FOG_OUTER_RADIUS) &&
                (Game::Read<uint32_t>(fog, Offsets::OFF_MAPOBJ_FOG_FLAGS) & Offsets::MAPOBJ_FOG_NOT_CANDIDATE) == 0)
                candidates[n++] = {distance, fog};
        }
    }
    std::sort(candidates, candidates + n,
              [](const Candidate &x, const Candidate &y) { return x.distance > y.distance; });
    for (uint32_t i = 0; i < n; ++i) {
        const float w = reinterpret_cast<Falloff_t>(Offsets::FUN_LIGHT_FALLOFF)(candidates[i].fog,
                                                                              candidates[i].distance);
        LerpFog(out, candidates[i].fog + Offsets::OFF_MAPOBJ_FOG_END, w);
    }
    dry = out;
    return inside;
}

// BuildSceneFog (0x006CEE30) for the scene's camera, dry (the camera's
// liquid is not asked): with the claim, the target is the dry record, its
// end capped at the far clip and its start the fraction of that; the weight
// moves toward 1 while the camera is inside, else toward 0, and the target
// stays the last one while it falls. The world starts the weight at 0 and
// fades in; a scene's first update takes it as it stands.
void UpdateCameraFog(Backdrop &b, const float (&eye)[3], float farClip, float elapsed) {
    CameraFog &fog = b.fog;
    uint32_t groups[2] = {kNoGroup, kNoGroup};
    const uint32_t count = HasOwnFog(b) ? ClaimCamera(b, eye, groups) : 0;
    bool inside = false;
    if (count != 0) {
        FogValues dry{};
        inside = InteriorFog(b, eye, groups, count, dry);
        fog.target.end = dry.end < farClip ? dry.end : farClip;
        fog.target.start = fog.target.end * dry.start;
        fog.target.color = dry.color;
    }
    const float step = EngineFloat(Offsets::VAR_INTERIOR_FOG_RATE) * elapsed;
    if (!fog.primed) {
        fog.primed = true;
        fog.weight = inside ? 1.0f : 0.0f;
    } else if (inside) {
        fog.weight = fog.weight + step;
        if (!(fog.weight >= 0.0f))
            fog.weight = 0.0f;
        else if (fog.weight >= 1.0f)
            fog.weight = 1.0f;
    } else {
        fog.weight = fog.weight - step;
        if (!(fog.weight > 0.0f))
            fog.weight = 0.0f;
    }
}

// ---- the group liquids ----------------------------------------------------------------

using Fn_t = void(__cdecl *)();
using Unit_t = void(__fastcall *)(int unit);
using Load_t = void(__fastcall *)(int unit, const float *m);
constexpr int kWorldStack = 8;

// The render states pushed and world stack 8 = placement x Translate(-eye),
// as the world's map object passes set it.
void PushObjectSpace(const Backdrop &b, const float (&eye)[3]) {
    Matrix toEye, world;
    Identity(toEye);
    Translate(toEye, {-eye[0], -eye[1], -eye[2]});
    Multiply(world, b.placement, toEye);
    reinterpret_cast<Fn_t>(Offsets::FUN_GX_STATE_PUSH)();
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_PUSH)(kWorldStack);
    reinterpret_cast<Load_t>(Offsets::FUN_GX_TEXMTX_LOAD)(kWorldStack, world);
}

void PopObjectSpace() {
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_POP)(kWorldStack);
    reinterpret_cast<Fn_t>(Offsets::FUN_GX_STATE_POP)();
}

// AttachEntityToGround (0x006A8A20), the map object leg (a scene has no
// terrain), with SyncLightNode's box, centre and radius and UpdateNode's
// probe point (0x00613F80).
void Place(const Backdrop &b, Ambience &a, void *frame, void *model, const float *base) {
    using TraceFace_t = bool(__thiscall *)(const void *def, const float *start, const float *end, uint32_t group,
                                           uint16_t *face, float *hit, float *t);
    using TerrainType_t = int32_t(__thiscall *)(const void *def, uint32_t group, uint16_t face);
    using Sample_t = bool(__thiscall *)(void *object, uint32_t group, const float *point, uint16_t face,
                                        uint32_t *bgra, bool *day);
    using Split_t = void(__stdcall *)(const uint32_t *src, uint32_t *first, uint8_t floor, uint32_t *second,
                                      uint8_t cap);
    using CollisionBox_t = void(__thiscall *)(void *model, float *box);
    void *object = b.object;
    a.lights.clear();
    a.interior = false;
    a.ground = -1;

    // SyncLightNode: the model's bounds through its matrix, their centre and
    // radius, or the translation when the bounds are empty.
    float box[6];
    M2::GetBoundingBox(model, box);
    float centre[3];
    float radius = 0.0f;
    if (box[0] > box[3] || box[1] > box[4] || box[2] > box[5]) {
        std::memcpy(centre, a.position, sizeof centre);
    } else {
        float world[6];
        TransformBox(base, box, world);
        for (int i = 0; i < 3; ++i)
            centre[i] = (world[i] + world[i + 3]) * 0.5f;
        const float dx = world[3] - centre[0], dy = world[4] - centre[1], dz = world[5] - centre[2];
        radius = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    // The probe point: the collision box's centre through the matrix.
    float point[3] = {0.0f, 0.0f, 0.0f};
    if (M2::IsLoaded(model, 0, 0)) {
        float collision[6];
        reinterpret_cast<CollisionBox_t>(Offsets::FUN_M2MODEL_GET_COLLISION_BOX)(model, collision);
        for (int i = 0; i < 3; ++i)
            point[i] = (collision[i + 3] + collision[i]) * 0.5f;
    }
    float start[3];
    TransformPoint(point, *reinterpret_cast<const Matrix *>(base), start);

    const float length = EngineFloat(Offsets::VAR_ENTITY_PROBE_LENGTH);
    float end[3] = {start[0], start[1], start[2] - length};
    float t = 1.0f;
    uint32_t group = 0;
    uint16_t face = 0;
    if (!Nearest(b, start, end, t, group, face)) {
        end[0] = centre[0];
        end[1] = centre[1];
        end[2] = radius + length + centre[2];
        if (!Nearest(b, start, end, t, group, face))
            return; // links nothing: the sun, no ground
    }
    // The hit group links the unit: its lights light it.
    if (group < b.groupLights.size())
        for (const MapLight &light : b.groupLights[group])
            AddMapLight(a.position, light, a.lights);
    void *loaded = LoadedGroup(object, group);
    const uint32_t flags = Game::Read<uint32_t>(loaded, Offsets::OFF_CMAPOBJGROUP_FLAGS);
    const bool interior = (flags & (Offsets::MAPOBJ_GROUP_EXTERIOR | Offsets::MAPOBJ_GROUP_EXTERIOR_LIT)) == 0;
    a.interior = interior;
    float hit[3] = {0.0f, 0.0f, 0.0f};
    float faceT = 1.0f;
    if (!reinterpret_cast<TraceFace_t>(Offsets::FUN_CMAPOBJDEF_TRACE_GROUP_FACE)(b.def, start, end, group, &face,
                                                                                hit, &faceT))
        return; // no ground; an interior unit keeps the bytes it had
    a.ground = reinterpret_cast<TerrainType_t>(Offsets::FUN_CMAPOBJDEF_GET_FACE_TERRAIN_TYPE)(b.def, group, face);
    if (!interior)
        return;
    // BakeGroupLight's face path (0x006A8410).
    float sample[3];
    TransformPoint(hit, b.inverse, sample);
    uint32_t color = 0;
    bool day = false;
    if (!reinterpret_cast<Sample_t>(Offsets::FUN_CMAPOBJ_SAMPLE_GROUP_COLOR)(object, group, sample, face, &color,
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

int Ground(const Backdrop *b, const Ambience &a) { return b != nullptr && a.serial == b->serial ? a.ground : -1; }

namespace {

// TestMapObjectBox (0x006AD330) for the one WMO: the box into the object's
// space (centre through the inverse, half extents through its axes), then
// the facets of the groups it touches into the batches.
bool CollectBox(const Backdrop &b, const float (&box)[6], uint32_t mask) {
    using RotateBox_t = void(__fastcall *)(const float *axes, const float *box, float *out);
    using Overlaps_t = bool(__thiscall *)(void *object, const float *box);
    using Collect_t = bool(__thiscall *)(void *object, void *out, const float *box, const void *def, uint32_t mask);
    float centre[3], half[6];
    for (int i = 0; i < 3; ++i)
        centre[i] = (box[i] + box[i + 3]) * 0.5f;
    for (int i = 0; i < 6; ++i)
        half[i] = box[i] - centre[i % 3];
    float moved[3];
    TransformPoint(centre, b.inverse, moved);
    const float *m = b.inverse;
    const float axes[9] = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
    float local[6];
    reinterpret_cast<RotateBox_t>(Offsets::FUN_ROTATE_BOX)(axes, half, local);
    for (int i = 0; i < 6; ++i)
        local[i] += moved[i % 3];
    if (!reinterpret_cast<Overlaps_t>(Offsets::FUN_CMAPOBJ_BOX_OVERLAPS)(b.object, local))
        return false;
    uint32_t out = 0;
    return reinterpret_cast<Collect_t>(Offsets::FUN_CMAPOBJ_COLLECT_BOX_FACETS)(b.object, &out, local, b.def, mask);
}

// A texture matrix built for absolute positions, for positions relative to
// the eye (what texgen gives, the vertices being relative to it):
// Translate(eye) x m.
void FromEye(Matrix &m, const float (&eye)[3]) {
    for (int j = 0; j < 4; ++j)
        m[12 + j] += eye[0] * m[j] + eye[1] * m[4 + j] + eye[2] * m[8 + j];
}

// DrawDecalBatches (0x006D7480) over the WMO's facets, the eye for the
// world camera.
void DrawBatches(const Backdrop &b, const float (&box)[6], uint32_t argb, uint32_t mask, const float (&eye)[3]) {
    using GetInt_t = void(__fastcall *)(int id, int *out);
    using Set_t = void(__fastcall *)(int id, int value);
    using SetPtr_t = void(__fastcall *)(int id, const void *value);
    using SetWord_t = void(__fastcall *)(int id, int, uint32_t value);
    using SetFloat_t = void(__fastcall *)(int id, int, float value);
    using Caps_t = const uint8_t *(__fastcall *)();
    using WriteColored_t = void(__fastcall *)(const void *batch);
    using Write_t = void(__thiscall *)(const void *batch, uint32_t color);
    using Indices_t = void(__fastcall *)(const void *batch);
    struct Prim {
        uint32_t type, base;
        uint16_t count, min, max, unused;
    };
    using Render_t = void(__fastcall *)(const Prim *prim, int count);

    ResetFacets();
    if (!CollectBox(b, box, mask))
        return;
    const auto getInt = reinterpret_cast<GetInt_t>(Offsets::FUN_GX_RS_GET_INT);
    const auto set = reinterpret_cast<Set_t>(Offsets::FUN_GX_RS_SET);
    const auto setPtr = reinterpret_cast<SetPtr_t>(Offsets::FUN_GX_RS_SET_PTR);
    reinterpret_cast<Fn_t>(Offsets::FUN_GX_STATE_PUSH)();
    int blend = 0;
    getInt(Offsets::GXRS_BLEND, &blend);
    if (blend == Offsets::GXRS_BLEND_MODULATE) {
        setPtr(Offsets::GXRS_TEXTURE0 + 1, Game::Read<void *>(Offsets::VAR_SHADOW_GREY_RAMP));
        set(Offsets::GXRS_COMBINER0 + 1, Offsets::GXRS_COMBINE_ADD);
    } else {
        setPtr(Offsets::GXRS_TEXTURE0 + 1, Game::Read<void *>(Offsets::VAR_SHADOW_RAMP));
    }
    const float bias = Game::Read<float>(Offsets::VAR_SHADOW_BIAS);
    if (bias != 0.0f)
        reinterpret_cast<SetFloat_t>(Offsets::FUN_GX_RS_SET_FLOAT)(
            Offsets::GXRS_DEPTH_BIAS, 0, (bias + bias) * Game::Read<float>(Offsets::VAR_SHADOW_BIAS_SCALE));
    for (int unit = 0; unit < 2; ++unit) {
        set(Offsets::GXRS_TEXGEN0 + unit, Offsets::GXRS_TEXGEN_WORLD_POS);
        set(Offsets::GXRS_TEXXFORM0 + unit, Offsets::GXRS_TEXXFORM_MATRIX);
    }
    uint32_t color = argb;
    int lighting = 0;
    getInt(Offsets::GXRS_LIGHTING, &lighting);
    if (lighting == 1) {
        reinterpret_cast<SetWord_t>(Offsets::FUN_GX_RS_SET_WORD)(Offsets::GXRS_MATERIAL, 0, argb);
    } else if (Game::Read<int>(reinterpret_cast<Caps_t>(Offsets::FUN_GX_DEVICE_CAPS)(),
                               Offsets::OFF_GXCAPS_COLOR_SWAP) == 1) {
        color = (argb & 0xFF00FF00u) | (argb & 0xFFu) << 16 | (argb >> 16 & 0xFFu);
    }
    Matrix toEye;
    Identity(toEye);
    Translate(toEye, {-eye[0], -eye[1], -eye[2]});
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_PUSH)(kWorldStack);
    const uint32_t count = Game::Read<uint32_t>(Offsets::VAR_FACET_BATCH_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *batch = FacetBatch(i);
        const uint16_t indices = Game::Read<uint16_t>(batch, Offsets::OFF_FACET_BATCH_INDEX_COUNT);
        const auto *matrix = Game::Read<const float *>(batch, Offsets::OFF_FACET_BATCH_MATRIX);
        if (static_cast<uint16_t>(indices * 3) > 0x8000 || matrix == nullptr ||
            Game::Read<const void *>(batch, Offsets::OFF_FACET_BATCH_VERTICES) == nullptr ||
            Game::Read<const void *>(batch, Offsets::OFF_FACET_BATCH_INDICES) == nullptr)
            continue;
        Matrix world;
        Multiply(world, *reinterpret_cast<const Matrix *>(matrix), toEye);
        reinterpret_cast<Load_t>(Offsets::FUN_GX_TEXMTX_LOAD)(kWorldStack, world);
        if (lighting == 1)
            reinterpret_cast<WriteColored_t>(Offsets::FUN_WRITE_DECAL_VERTICES_COLOURED)(batch);
        else
            reinterpret_cast<Write_t>(Offsets::FUN_WRITE_DECAL_VERTICES)(batch, color);
        reinterpret_cast<Indices_t>(Offsets::FUN_WRITE_DECAL_INDICES)(batch);
        const Prim prim{Offsets::GX_PRIM_TRIANGLES, 0, indices, 0, static_cast<uint16_t>(indices - 1), 0};
        reinterpret_cast<Render_t>(Offsets::FUN_GX_PRIM_RENDER_ONE)(&prim, 1);
    }
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_POP)(kWorldStack);
    reinterpret_cast<Fn_t>(Offsets::FUN_GX_STATE_POP)();
}

} // namespace

// DrawDecal (0x006D7330) onto the WMO: the transforms built for absolute
// positions (the other mode reads the world's camera), moved to the eye.
void DrawDecal(const Backdrop &b, const float (&box)[6], uint32_t argb, const float (&m)[16], float fade,
               uint32_t mask, const float (&eye)[3]) {
    if (Game::Read<uint8_t>(b.object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return;
    using Build_t = void(__fastcall *)(float *tex0, float *tex1, const float *box, const float *m, float fade,
                                       int absolute);
    Matrix tex0, tex1;
    Identity(tex0);
    Identity(tex1);
    reinterpret_cast<Build_t>(Offsets::FUN_BUILD_DECAL_TRANSFORMS)(tex0, tex1, box, m, fade, 1);
    FromEye(tex0, eye);
    FromEye(tex1, eye);
    reinterpret_cast<Load_t>(Offsets::FUN_GX_TEXMTX_PUSH_LOAD)(0, tex0);
    reinterpret_cast<Load_t>(Offsets::FUN_GX_TEXMTX_PUSH_LOAD)(1, tex1);
    DrawBatches(b, box, argb, mask, eye);
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_POP)(0);
    reinterpret_cast<Unit_t>(Offsets::FUN_GX_TEXMTX_POP)(1);
}

void Update(Backdrop &b, void *frame, bool sounds, float elapsed) {
    const View view = CameraView(frame);
    b.farClip = view.farClip;
    b.sounds = sounds && Game::Read<uint8_t>(Offsets::VAR_IN_WORLD) != 0;
    if (!b.sounds)
        Silence(b);
    void *object = b.object;
    if (Game::Read<uint8_t>(object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return;
    UpdateCameraFog(b, view.eye, view.farClip, elapsed);
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

void Draw(Backdrop &b, void *frame, const float (&eye)[3]) {
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
    PushObjectSpace(b, eye);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_PUSH)();
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_FRUSTUM_STORE)(volume);
    reinterpret_cast<void(__fastcall *)(const float *)>(Offsets::FUN_FRUSTUM_TRANSFORM)(b.inverse);
    CommitLighting(b, frame, eye);
    DrawGroups(b, frame);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_POP)();
    PopObjectSpace();
}

// DrawGroupLiquidPass (0x00684CD0) over the one WMO's parsed liquid groups.
// The world commits a record per group at the group def's sphere; here the
// WMO's one record (the drawn fog, the lights near the WMO) serves them all.
// Magma and slime draw unlit and water takes its colours from the world's
// light state, so the record gives them their fog.
void DrawLiquids(Backdrop &b, void *frame, const float (&eye)[3]) {
    using Type_t = uint32_t(__fastcall *)(const void *group);
    using DrawLiquid_t = void(__thiscall *)(void *object, uint32_t group, uint32_t parity);
    void *object = b.object;
    if (Game::Read<uint8_t>(object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return;
    std::vector<uint32_t> liquids;
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        const void *group = LoadedGroup(object, i);
        if (group != nullptr &&
            (Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_FLAGS) & Offsets::MAPOBJ_GROUP_LIQUID) != 0 &&
            (reinterpret_cast<Type_t>(Offsets::FUN_MAPOBJ_FIRST_LIQUID_TYPE)(group) & 0xFF) <=
                Offsets::MAPOBJ_LIQUID_TYPE_LAST_DRAWN)
            liquids.push_back(i);
    }
    if (liquids.empty())
        return;
    reinterpret_cast<Fn_t>(Offsets::FUN_MAPOBJ_BUILD_LIQUID_DEPTH_RAMP)();
    uint32_t &fogFlag = Game::Ref<uint32_t>(Offsets::VAR_MAPOBJ_GROUP_FOG_FLAG);
    const uint32_t savedFogFlag = fogFlag;
    fogFlag = 0;
    PushObjectSpace(b, eye);
    CommitLighting(b, frame, eye);
    for (const uint32_t i : liquids)
        reinterpret_cast<DrawLiquid_t>(Offsets::FUN_MAPOBJ_DRAW_GROUP_LIQUID)(object, i, 1);
    PopObjectSpace();
    fogFlag = savedFogFlag;
}

} // namespace Model::Scene::World
