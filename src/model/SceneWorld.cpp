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
// falls as it leaves (BuildSceneFog 0x006CEE30). In the WMO's water the
// target is the record's underwater fog, and in its magma or slime that fog
// when the record's flags allow it, else the zone's. The zone's fog in a scene
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
// mirrored: the water clip slot (0x006A7650), and ApplyEntityLighting's light
// gate (0x00672A20), which skips the light of a model whose sphere lies past
// the farclip setting (0x00C62964, stored by the farclip cvar at 0x0066F899):
// such a model is not drawn. A doodad is solid to the scene's bodies once its
// model file has loaded, as the world's are: its M2 collision mesh in the
// facet query (GatherDoodadFacets 0x006ABC40), by its group.
//
// Spell models in flight or left behind (missiles, impact models) are lit as
// the world lights them, as entities of their own standing on the WMO
// (CreateLightObjectB 0x00671010); a model attached to another (equipment, a
// stuck arrow, the ammo in hand) takes its parent's callback.
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
#include "gx/Matrix.h"
#include "model/M2.h"
#include "model/SceneContact.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
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
    const Backdrop *backdrop; // its WMO, or null for a map's own doodad (a prop)
    const bool *sounds;       // its owner's SetPlayWorldModelSounds, in the world
    void *frame;
    void *model; // one reference, or null when the file did not make one
    bool exterior;
    uint32_t ambient;     // BGRA bytes, SplitLightColor's
    uint32_t directional; // BGRA bytes
    float position[3];    // the def's +0x24: the record's position placed
    float matrix[16];     // the def's +0xCC: the record's placement, then the WMO's
    float scale;          // the record's
    // The def's bounds, set once the model file has loaded (the pending
    // defs' finish 0x00698C50, which also sets the def's loaded bit 0x80):
    // its sphere (+0x5C, +0x68) and its collision box through the matrix
    // (+0x14C).
    bool bounded;
    float center[3];
    float radius;
    float collision[6];
    uint32_t stamp;        // the def's +0x8C: the last facet query that took it
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
    bool zoneIsTarget = false; // the camera in magma or slime that has a fog of its own
};

struct Backdrop {
    void *object; // CMapObj, one reference ours
    float placement[16];
    float inverse[16];
    uint32_t doodadSet;
    uint32_t mapID; // the map whose light its outdoor water takes, or kNoMap
    // The fields of a map object def the engine's queries read (the
    // placement, its inverse, the object), at a def's offsets, set once.
    alignas(16) uint8_t def[Offsets::MAPOBJDEF_QUERY_SIZE] = {};
    std::vector<std::unique_ptr<Doodad>> doodads;
    std::vector<Doodad *> byIndex; // MODD index -> its doodad
    std::vector<bool> groupsMade;
    // Per group, its doodads in the order its def group links them (the
    // def group's list +0xA8; a doodad two groups hold is in both).
    std::vector<std::vector<Doodad *>> groupDoodads;
    uint32_t queryId = 0; // s_mapQueryId: one more for each facet query
    std::vector<std::vector<MapLight>> groupLights; // per group, once made
    uint32_t serial = 0;     // this backdrop among all loaded, for the actors' caches
    uint32_t generation = 0; // one more each time a group's doodads and lights are made
    float farClip = 0.0f;
    bool sounds = false; // SetPlayWorldModelSounds, in the world
    CameraFog fog;
    // The camera's liquid type (0xF dry): the claim's first group's, else
    // the scene's at the camera's probe point (OutdoorCameraLiquid).
    uint32_t cameraLiquid = Offsets::LIQUID_DRY;
    // The interior groups that hold the camera (the world's claim,
    // 0x00C7B748, and its group set 0x00C7CD88): the world then draws from
    // inside, and its portal walk from them decides the sky.
    uint32_t cameraGroups[2] = {};
    uint32_t cameraGroupCount = 0;
};

static_assert(Offsets::OFF_MAPOBJDEF_OBJECT + sizeof(void *) <= Offsets::MAPOBJDEF_QUERY_SIZE &&
                  Offsets::OFF_MAPOBJDEF_INVERSE + 16 * sizeof(float) <= Offsets::OFF_MAPOBJDEF_OBJECT,
              "the def block holds the three fields at their offsets");

// An actor's place in the WMO (Settle), as the world keeps it on the unit's
// map entity.
struct Ambience {
    uint32_t serial = 0; // the backdrop's, 0 before the first placement or without one
    uint32_t terrainSerial = 0; // the terrain's, 0 without one
    const void *model = nullptr;
    uint32_t generation = 0;
    float position[3] = {};
    bool interior = false;  // baked from a face; else the sun
    uint32_t ambient = 0;   // BGRA bytes (+0x9C), easing toward the target
    uint32_t ambientTarget = 0; // (+0xF4)
    float intensity = 1.0f;       // the directional's scale (+0xA4), easing
    float intensityTarget = 1.0f; // (+0xF8)
    uint32_t directional = 0; // BGRA bytes (+0xA0)
    uint32_t dayWeight = 0; // a day face's blend toward the sun (+0xA3)
    int32_t ground = -1;    // the face's TerrainType id (+0xDC), or -1
    // The liquid it stands in, outdoors (+0x90 bits 0x20 and 0x80, +0x98).
    bool inLiquid = false;
    bool splashes = false; // +0x90 bit 0x80, LiquidType flag 0x2
    bool ripples = false;  // +0x90 bit 0x40, LiquidType flag 0x1
    float liquidHeight = 0.0f;
    std::vector<Light> lights;
};

namespace {

using Gx::Identity;
using Gx::Matrix;
using Gx::Multiply;
using Gx::Rotate;
using Gx::TransformBox;
using Gx::TransformPoint;
using Gx::Translate;

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
FogValues SceneZoneFog(const Backdrop &b, void *frame) {
    if ((Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) & Offsets::SIMPLEMODEL_FOG_ARMED) != 0)
        return {Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR),
                Game::Read<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR),
                Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR)};
    return {b.farClip, b.farClip, b.fog.target.color};
}

// The camera in magma or slime with a fog of its own makes the zone's fog the
// target too.
FogValues ZoneFog(const Backdrop &b, void *frame) {
    return b.fog.zoneIsTarget ? b.fog.target : SceneZoneFog(b, frame);
}

// The drawn fog (the light state's +0x80).
FogValues DrawnFog(const Backdrop &b, void *frame) {
    FogValues drawn = ZoneFog(b, frame);
    LerpFog(drawn, &b.fog.target, b.fog.weight);
    return drawn;
}

void WmoFog(const Backdrop &b, void *record, void *frame) { SetFog(record, DrawnFog(b, frame)); }

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
    // A record without fog turns the device fog off and leaves its range,
    // but the group drawers turn the fog back on for every material without
    // flag 0x2: the world always commits a fog first. With the device's own
    // range (0 to 1, in 0xFFFF0080) a scene without fog would draw them in
    // that colour, so it gets a range from the far clip to where a world
    // without light ends its fog, which fogs nothing it draws. The caller's
    // render-state scope ends it.
    if (!WmoFogShown(b) &&
        (Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) & Offsets::SIMPLEMODEL_FOG_ARMED) == 0) {
        Gx::SetStateFloat(Offsets::GXRS_FOG_START, b.farClip);
        Gx::SetStateFloat(Offsets::GXRS_FOG_END, Game::Read<float>(Offsets::VAR_NO_LIGHT_FOG_END));
    }
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

void ReadVector(const uint8_t *at, float (&out)[3]) { std::memcpy(out, at, sizeof out); }

// BGRA bytes as the rgb the record adds (byte / 255).
void ColorOf(uint32_t bgra, float (&rgb)[3]) {
    const float k = EngineFloat(Offsets::VAR_ONE_OVER_255);
    rgb[0] = static_cast<float>((bgra >> 16) & 0xFF) * k;
    rgb[1] = static_cast<float>((bgra >> 8) & 0xFF) * k;
    rgb[2] = static_cast<float>(bgra & 0xFF) * k;
}


MapLight MakeMapLight(const Backdrop &b, const uint8_t *record) {
    MapLight m{};
    void *light = m.light.bytes;
    M2::InitLight(light);
    float position[3];
    ReadVector(record + Offsets::OFF_MOLT_POSITION, position);
    TransformPoint(position, b.placement, position);
    m.start = Game::Read<float>(record, Offsets::OFF_MOLT_START);
    m.end = Game::Read<float>(record, Offsets::OFF_MOLT_END);
    if (std::fabs(m.start - m.end) < EngineFloat(Offsets::VAR_ONE_THOUSANDTH))
        m.end += EngineFloat(Offsets::VAR_ONE_TENTH);
    m.falloff = EngineFloat(Offsets::VAR_ONE) / (m.end - m.start);
    M2::SetLightEnabled(light, true);
    M2::SetLightType(light, 1);
    M2::SetLightPosition(light, position);
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
    M2::SetLightDirection(light, direction);
    float *diffuse = reinterpret_cast<float *>(added.bytes + Offsets::OFF_M2LIGHT_DIFFUSE);
    for (int i = 0; i < 3; ++i)
        diffuse[i] *= intensity;
    M2::SetLightEnabled(light, true);
    M2::SetLightType(light, 0);
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
                   const float (&direction)[3], float intensity) {
    if (WmoFogShown(b))
        WmoFog(b, record, frame);
    else
        SceneFog(record, frame);
    float ambient[3], directional[3];
    ColorOf(ambientBytes, ambient);
    ColorOf(directionalBytes, directional);
    for (float &c : directional)
        c *= intensity;
    reinterpret_cast<void(__thiscall *)(void *, const float *)>(Offsets::FUN_M2LIGHTING_ADD_AMBIENT)(record, ambient);
    reinterpret_cast<void(__thiscall *)(void *, const float *, const float *)>(Offsets::FUN_M2LIGHTING_ADD_DIRECTIONAL)(
        record, directional, direction);
}

// The model's lighting callback, `__fastcall(instance, record, user)` ret 4,
// as the world's ApplyEntityLighting (0x00672A20) runs a def's fog and light
// slots.
void __fastcall DoodadLighting(void * /*instance*/, void *record, Doodad *d) {
    if (d->exterior)
        FrameLightAndFog(record, d->frame);
    else
        InteriorLight(*d->backdrop, record, d->frame, d->ambient, d->directional, MapLightDirection(),
                      EngineFloat(Offsets::VAR_ONE));
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
    if (!*d.sounds)
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

// CreateDoodadModel (0x00695100) in the frame's scene, at the def's matrix.
void MakeModel(Doodad &d, const char *path, const Matrix &m) {
    d.model = M2::CreateModel(Model::Pane::Scene(d.frame), path);
    if (d.model == nullptr)
        return;
    M2::SetBaseTransform(d.model, m);
    const auto user = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&d));
    M2::SetAnimDoneCallback(d.model, reinterpret_cast<const void *>(Offsets::FUN_DOODAD_MODEL_DONE), 0, 0);
    M2::SetEventCallback(d.model, reinterpret_cast<const void *>(&OnDoodadEvent), user, 0);
    M2::SetRenderCallback(d.model, reinterpret_cast<const void *>(&DoodadLighting), &d);
    M2::PlayAnimation(d.model, -1, 0, -1, 0, 1.0f, 1, 1);
}

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
    d.sounds = &b.sounds;
    d.frame = frame;
    float position[3];
    ReadVector(record + Offsets::OFF_MODD_POSITION, position);
    TransformPoint(position, b.placement, d.position);
    Matrix m;
    Identity(m);
    Translate(m, position);
    Gx::RotateQuaternion(m, Game::Ptr<float>(record, Offsets::OFF_MODD_ROTATION));
    Gx::Scale(m, Game::Read<float>(record, Offsets::OFF_MODD_SCALE));
    Gx::MultiplyInPlace(m, b.placement);
    std::memcpy(d.matrix, m, sizeof d.matrix);
    d.scale = Game::Read<float>(record, Offsets::OFF_MODD_SCALE);
    using Split_t = void(__stdcall *)(const uint32_t *src, uint32_t *first, uint8_t floor, uint32_t *second,
                                      uint8_t cap);
    reinterpret_cast<Split_t>(Offsets::FUN_SPLIT_LIGHT_COLOR)(Game::Ptr<uint32_t>(record, Offsets::OFF_MODD_COLOR),
                                                             &d.directional, Offsets::DOODAD_DIRECTIONAL_FLOOR,
                                                             &d.ambient, Offsets::DOODAD_AMBIENT_CAP);
    MakeModel(d, path, m);
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
        b.groupDoodads[groupIndex].push_back(d);
        d->exterior = d->exterior || exterior;
        for (const MapLight &light : lights)
            AddMapLight(d->position, light, d->lights);
    }
    b.groupLights[groupIndex] = std::move(lights);
    ++b.generation;
}

// SetDoodadDefBounds (0x006952A0), for a loaded model: the sphere, and the
// collision box through the matrix.
void Bound(Doodad &d) {
    float sphere[4];
    M2::GetBoundingSphere(d.model, sphere);
    const float centre[3] = {sphere[0], sphere[1], sphere[2]};
    TransformPoint(centre, d.matrix, d.center);
    d.radius = sphere[3] * d.scale;
    float collision[6];
    M2::GetCollisionBox(d.model, collision);
    TransformBox(d.matrix, collision, d.collision);
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

// The animate list empties at each paint (as for the actors' models), and the
// world shows each doodad it draws again each frame, faded by distance
// (DrawDoodadModels, 0x00683F80).
void ShowDoodads(const std::vector<std::unique_ptr<Doodad>> &doodads, const View &view) {
    for (const auto &d : doodads) {
        // A def's bounds are set once its model file has loaded (0x00698C50).
        if (d->model != nullptr && !d->bounded && M2::IsLoaded(d->model, 0, 0))
            Bound(*d);
        if (d->model != nullptr && d->bounded && M2::IsReady(d->model, 0, 1)) {
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

void ViewVolume(uint8_t (&volume)[Offsets::FRUSTUM_VOLUME_SIZE], const float (&eye)[3]) {
    BuildViewVolume(volume, eye);
}

float ProbeDrop(const float (&eye)[3]) {
    alignas(16) uint8_t volume[Offsets::FRUSTUM_VOLUME_SIZE] = {};
    BuildViewVolume(volume, eye);
    // The camera's probe z (0x006809E2): a near corner at or below the
    // lowest so far, or NaN, replaces it.
    const auto *corners = reinterpret_cast<const float *>(volume + Offsets::OFF_FRUSTUM_VOLUME_CORNERS);
    float lowest = eye[2];
    for (int i = 0; i < 4; ++i)
        if (!(corners[i * 3 + 2] > lowest))
            lowest = corners[i * 3 + 2];
    return eye[2] - lowest;
}

void FrameFog(void *record, void *frame) { SceneFog(record, frame); }

bool IsWmoPath(const char *path) { return path != nullptr && HasWmoSuffix(path); }

Backdrop *Load(const char *path, const float (&placement)[16], uint32_t doodadSet, uint32_t mapID) {
    if (!IsWmoPath(path) || CheckedGroupCount(path) < 0)
        return nullptr;
    void *object = reinterpret_cast<void *(__fastcall *)(const char *)>(Offsets::FUN_CMAPOBJ_GET)(path);
    if (object == nullptr)
        return nullptr;
    static uint32_t serials = 0;
    auto *b = new Backdrop{object, {}, {}, doodadSet, mapID};
    b->serial = ++serials;
    std::memcpy(b->placement, placement, sizeof b->placement);
    Gx::InvertAffine(b->placement, b->inverse, 1.0f);
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
    return Load(name.data(), m, doodadSet, mapID);
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

} // namespace

// What every facet query does first (0x006A893B, 0x006D7497, 0x006AB4F2).
void ResetFacetBatches() {
    Game::Ref<uint32_t>(Offsets::VAR_FACET_OVERFLOW) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_BATCH_COUNT) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_FACE_CURSOR) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_INDEX_CURSOR) = 0;
    Game::Ref<uint32_t>(Offsets::VAR_FACET_MATRIX_COUNT) = 0;
}

namespace {

const uint8_t *FacetBatch(uint32_t index) {
    return reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Offsets::VAR_FACET_BATCHES) +
                                             index * Offsets::FACET_BATCH_SIZE);
}

// TraceMapObjectsNearest (0x006A8840) over the one WMO: the nearest group
// face the segment crosses (mask 4), its group and the first face of the
// group's batch. The world first screens the segment against the instance's
// world box (0x006C3330); each group's own box test below already covers
// it, so it is left out. `normal`, when given, takes the hit triangle's
// normal in WMO space, unscaled and either way round, read from batch 0 as
// CMapObj::IntersectSegment (0x006A37B0) reads it.
bool Nearest(const Backdrop &b, const float (&start)[3], const float (&end)[3], float &t, uint32_t &group,
             uint16_t &face, float (*normal)[3] = nullptr,
             uint16_t faceMask = Offsets::MAPOBJ_TRACE_MASK_GROUND) {
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
        ResetFacetBatches();
        float segment[6];
        std::memcpy(segment, local, sizeof segment);
        float tt = t;
        uint32_t out = 0;
        if (reinterpret_cast<Intersect_t>(Offsets::FUN_CMAPOBJGROUP_INTERSECT_SEGMENT)(
                loaded, &out, segment, &tt, b.def, 0, faceMask) &&
            tt < t) {
            t = tt;
            group = g;
            found = true;
            const uint8_t *batch = FacetBatch(0);
            if (const auto *faces = Game::Read<const uint16_t *>(batch, Offsets::OFF_FACET_BATCH_FACES)) {
                face = faces[0];
                if (normal != nullptr) {
                    const auto *index = Game::Read<const uint16_t *>(batch, Offsets::OFF_FACET_BATCH_INDICES);
                    const auto *vertex = Game::Read<const float *>(batch, Offsets::OFF_FACET_BATCH_VERTICES);
                    const float *v0 = vertex + 3 * index[0], *v1 = vertex + 3 * index[1],
                                *v2 = vertex + 3 * index[2];
                    const float e1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
                    const float e2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
                    (*normal)[0] = e1[1] * e2[2] - e1[2] * e2[1];
                    (*normal)[1] = e1[2] * e2[0] - e1[0] * e2[2];
                    (*normal)[2] = e1[0] * e2[1] - e1[1] * e2[0];
                }
            }
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

// GetCameraInteriorFog (0x0069DE20) for the claim: the dry and underwater
// sub-records of record 0 blended with the first group's candidate fogs,
// farthest first (its heap's pop order), the flags of record 0 or of the
// nearest candidate, and whether a group of the claim is interior. The
// engine reads a fog index past the records without a check; one is skipped
// here.
bool InteriorFog(const Backdrop &b, const float (&eye)[3], const uint32_t (&groups)[2], uint32_t count,
                 FogValues (&out)[2], uint32_t &flags) {
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
    std::memcpy(out, fogs + Offsets::OFF_MAPOBJ_FOG_END, sizeof out);
    flags = Game::Read<uint32_t>(fogs, Offsets::OFF_MAPOBJ_FOG_FLAGS);
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
        LerpFog(out[0], candidates[i].fog + Offsets::OFF_MAPOBJ_FOG_END, w);
        LerpFog(out[1], candidates[i].fog + Offsets::OFF_MAPOBJ_FOG_UNDERWATER, w);
        if (i + 1 == n)
            flags = Game::Read<uint32_t>(candidates[i].fog, Offsets::OFF_MAPOBJ_FOG_FLAGS);
    }
    return inside;
}

// CameraEnvironment (0x006809C0) inside the WMO: the liquid of the camera's
// first group at the probe point, 0xF when dry.
uint32_t GroupCameraLiquid(const Backdrop &b, void *frame, const float (&eye)[3], uint32_t group) {
    using Height_t = bool(__thiscall *)(void *group, const float *point, uint32_t *type, float *height,
                                        float *vector);
    void *loaded = LoadedGroup(b.object, group);
    if (loaded == nullptr)
        return Offsets::LIQUID_DRY;
    const float probe[3] = {eye[0], eye[1], eye[2] - CameraProbeDrop(frame)};
    float point[3], height = 0.0f, vector[3] = {};
    TransformPoint(probe, b.inverse, point);
    uint32_t type = Offsets::LIQUID_DRY;
    reinterpret_cast<Height_t>(Offsets::FUN_CMAPOBJGROUP_LIQUID_HEIGHT)(loaded, point, &type, &height, vector);
    return type;
}

// BuildSceneFog (0x006CEE30) for the scene's camera: with the claim, the
// target is the dry record (the underwater one in water, and in magma or
// slime whose fog flags allow it), its end capped at the far clip and its
// start the fraction of that; magma or slime without the flag take the zone's
// fog as the target. The weight moves toward 1 while the camera is inside,
// else toward 0, and the target stays the last one while it falls. The world
// starts the weight at 0 and fades in; a scene's first update takes it as it
// stands. The zone's fog under a liquid is the map's for it (MapLiquidFog).
bool MapLiquidFog(uint32_t mapID, uint32_t cameraLiquid, const float (&eye)[3], float farClip, FogValues &out);

void UpdateCameraFog(Backdrop &b, void *frame, const float (&eye)[3], float farClip, float elapsed) {
    CameraFog &fog = b.fog;
    uint32_t groups[2] = {kNoGroup, kNoGroup};
    const uint32_t claimed = ClaimCamera(b, eye, groups);
    const uint32_t count = HasOwnFog(b) ? claimed : 0;
    bool inside = false;
    fog.zoneIsTarget = false;
    b.cameraGroupCount = claimed;
    std::memcpy(b.cameraGroups, groups, sizeof groups);
    // Without the claim, the world asks its liquid query at the probe point
    // (0x006723D0): the WMO's liquid, then the terrain's.
    b.cameraLiquid = claimed != 0 ? GroupCameraLiquid(b, frame, eye, groups[0]) : OutdoorCameraLiquid(frame);
    if (count != 0) {
        FogValues records[2] = {};
        uint32_t flags = 0;
        inside = InteriorFog(b, eye, groups, count, records, flags);
        const uint32_t liquid = b.cameraLiquid == Offsets::LIQUID_DRY ? b.cameraLiquid : (b.cameraLiquid & 3);
        const bool thick = liquid == Offsets::LIQUID_MAGMA || liquid == Offsets::LIQUID_SLIME;
        const bool own = liquid == Offsets::LIQUID_MAGMA ? (flags & Offsets::MAPOBJ_FOG_FLAG_MAGMA) != 0
                         : liquid == Offsets::LIQUID_SLIME ? (flags & Offsets::MAPOBJ_FOG_FLAG_SLIME) != 0
                                                           : true;
        if (!own) {
            if (!MapLiquidFog(b.mapID, b.cameraLiquid, eye, farClip, fog.target))
                fog.target = SceneZoneFog(b, frame);
        } else {
            const FogValues &record = records[liquid == Offsets::LIQUID_DRY ? 0 : 1];
            fog.target.end = record.end < farClip ? record.end : farClip;
            fog.target.start = fog.target.end * record.start;
            fog.target.color = record.color;
            fog.zoneIsTarget = thick;
        }
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



// The render states pushed and world stack 8 = placement x Translate(-eye),
// as the world's map object passes set it.
void PushObjectSpace(const Backdrop &b, const float (&eye)[3]) {
    Matrix toEye, world;
    Identity(toEye);
    Translate(toEye, {-eye[0], -eye[1], -eye[2]});
    Multiply(world, b.placement, toEye);
    Gx::PushStates();
    Gx::PushTransform(Offsets::GX_XFORM_WORLD);
    Gx::SetTransform(Offsets::GX_XFORM_WORLD, world);
}

void PopObjectSpace() {
    Gx::PopTransform(Offsets::GX_XFORM_WORLD);
    Gx::PopStates();
}

// AnimateMapEntityLight (0x0069E770): the ambient bytes and the intensity
// move toward their targets by the frame's time; settled outside, the
// ambient follows the day's (the scene light's).
void Ease(Ambience &a, void *frame, float elapsed) {
    int step = static_cast<int>(EngineFloat(Offsets::VAR_ENTITY_COLOR_RATE) * elapsed *
                                EngineFloat(Offsets::VAR_TWO_FIFTY_FIVE));
    if (step < 1)
        step = 1;
    bool moving = false;
    uint32_t eased = a.ambient;
    for (int shift = 0; shift <= 16; shift += 8) {
        int value = static_cast<int>((a.ambient >> shift) & 0xFF);
        const int target = static_cast<int>((a.ambientTarget >> shift) & 0xFF);
        if (target == value)
            continue;
        moving = true;
        value = target > value ? (std::min)(value + step, target) : (std::max)(value - step, target);
        eased = (eased & ~(0xFFu << shift)) | static_cast<uint32_t>(value) << shift;
    }
    a.ambient = eased;
    if (!a.interior && !moving)
        a.ambient = a.ambientTarget =
            SunBytes(Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT), Offsets::OFF_M2LIGHT_AMBIENT);
    const float rate = EngineFloat(Offsets::VAR_ENTITY_INTENSITY_RATE) * elapsed;
    const float d = a.intensity - a.intensityTarget;
    if (d < 0.0f) {
        a.intensity += rate;
        if (a.intensity > a.intensityTarget)
            a.intensity = a.intensityTarget;
    } else if (!(d == 0.0f)) {
        a.intensity -= rate;
        if (a.intensity < a.intensityTarget)
            a.intensity = a.intensityTarget;
    }
}

// AttachEntityToGround (0x006A8A20), the map object leg (a scene has no
// terrain), with SyncLightNode's box, centre and radius and UpdateNode's
// probe point (0x00613F80).
void Place(const Backdrop *wmo, const Terrain::Land *terrain, Ambience &a, void *frame, void *model,
           const float *base) {
    using TraceFace_t = bool(__thiscall *)(const void *def, const float *start, const float *end, uint32_t group,
                                           uint16_t *face, float *hit, float *t);
    using TerrainType_t = int32_t(__thiscall *)(const void *def, uint32_t group, uint16_t face);
    using Sample_t = bool(__thiscall *)(void *object, uint32_t group, const float *point, uint16_t face,
                                        uint32_t *bgra, bool *day);
    using Split_t = void(__stdcall *)(const uint32_t *src, uint32_t *first, uint8_t floor, uint32_t *second,
                                      uint8_t cap);
    a.lights.clear();
    a.interior = false;
    a.ground = -1;
    // Not indoors (0x0069E280): the day ambient and 2.5 are the targets.
    a.ambientTarget = SunBytes(Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT), Offsets::OFF_M2LIGHT_AMBIENT);
    a.intensityTarget = EngineFloat(Offsets::VAR_ENTITY_INTENSITY_LIT);

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
        M2::GetCollisionBox(model, collision);
        for (int i = 0; i < 3; ++i)
            point[i] = (collision[i + 3] + collision[i]) * 0.5f;
    }
    float start[3];
    TransformPoint(point, *reinterpret_cast<const Matrix *>(base), start);

    // AttachEntityToGround (0x006A8A20): the terrain and the map objects down
    // from the probe; with neither, up past the bounds; the terrain wins when
    // it is nearer.
    const float length = EngineFloat(Offsets::VAR_ONE_THOUSAND);
    float end[3] = {start[0], start[1], start[2] - length};
    float t = 1.0f;
    float terrainT = 1.0f;
    float normal[3];
    uint32_t group = 0;
    uint16_t face = 0;
    bool onTerrain = terrain != nullptr && Terrain::Trace(*terrain, start, end, terrainT, normal);
    bool onWmo = wmo != nullptr && Nearest(*wmo, start, end, t, group, face);
    if (!onTerrain && !onWmo) {
        end[0] = centre[0];
        end[1] = centre[1];
        end[2] = radius + length + centre[2];
        onTerrain = terrain != nullptr && Terrain::Trace(*terrain, start, end, terrainT, normal);
        onWmo = wmo != nullptr && Nearest(*wmo, start, end, t, group, face);
    }
    if (onTerrain && onWmo && terrainT < t)
        onWmo = false;
    if (!onWmo) {
        // The terrain links the unit outdoors, in the sun: the ground is the
        // dominant layer's under its position (GetGroundEffect 0x0069B1C0).
        int32_t type = -1;
        if (onTerrain && Terrain::GroundEffect(*terrain, a.position, type))
            a.ground = type;
        return; // with neither, it links nothing: the sun, no ground
    }
    const Backdrop &b = *wmo;
    void *object = b.object;
    // The hit group links the unit: its lights light it.
    if (group < b.groupLights.size())
        for (const MapLight &light : b.groupLights[group])
            AddMapLight(a.position, light, a.lights);
    void *loaded = LoadedGroup(object, group);
    const uint32_t flags = Game::Read<uint32_t>(loaded, Offsets::OFF_CMAPOBJGROUP_FLAGS);
    const bool interior = (flags & (Offsets::MAPOBJ_GROUP_EXTERIOR | Offsets::MAPOBJ_GROUP_EXTERIOR_LIT)) == 0;
    a.interior = interior;
    if (interior)
        a.intensityTarget = EngineFloat(Offsets::VAR_ONE);
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
                                                             Offsets::ENTITY_DIRECTIONAL_FLOOR, &a.ambientTarget,
                                                             Offsets::DOODAD_AMBIENT_CAP);
    a.dayWeight = 0;
    if (!day)
        return;
    const auto *sun = Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT);
    const uint32_t weight = color >> 24;
    if (weight != 0) {
        a.directional = BlendBytes(a.directional, SunBytes(sun, Offsets::OFF_M2LIGHT_DIFFUSE), weight);
        a.ambientTarget = BlendBytes(a.ambientTarget, SunBytes(sun, Offsets::OFF_M2LIGHT_AMBIENT), weight);
    }
    a.dayWeight = weight;
}

// UpdateMapEntityEnvironment's liquid leg (0x0069E2BF..0x0069E35F), after
// the placement: outdoors, the liquid at the actor's position, and whether
// the LiquidType of its area for that liquid splashes. The area is the
// terrain's under the position: the scene's WMO carries no name set, so its
// whole-WMO WMOAreaTable row is not looked up and the terrain answers, as
// for the world's map object without a row.
void LiquidEnvironment(const Terrain::Land *terrain, Ambience &a, void *frame) {
    using LiquidType_t = const uint8_t *(__fastcall *)(int32_t areaID, uint32_t type);
    a.inLiquid = false;
    a.splashes = false;
    a.ripples = false;
    uint32_t type = 0;
    float height = 0.0f;
    if (a.interior || !LiquidAtPoint(frame, a.position, type, height))
        return;
    a.inLiquid = true;
    a.liquidHeight = height;
    const uint32_t area = terrain != nullptr ? Terrain::AreaAt(*terrain, a.position) : 0;
    const uint8_t *row =
        reinterpret_cast<LiquidType_t>(Offsets::FUN_LIQUIDTYPE_FOR_AREA)(static_cast<int32_t>(area), type);
    const uint32_t flags = row != nullptr ? Game::Read<uint32_t>(row, Offsets::OFF_LIQUIDTYPE_FLAGS) : 0;
    a.splashes = (flags & Offsets::LIQUIDTYPE_FLAG_SPLASH) != 0;
    a.ripples = (flags & Offsets::LIQUIDTYPE_FLAG_RIPPLES) != 0;
}

} // namespace

Ambience *NewAmbience() { return new Ambience(); }

void FreeAmbience(Ambience *ambience) { delete ambience; }

void Settle(const Backdrop *b, const Terrain::Land *terrain, Ambience &a, void *frame, void *model, float elapsed) {
    if ((b == nullptr && terrain == nullptr) ||
        (b != nullptr && Game::Read<uint8_t>(b->object, Offsets::OFF_CMAPOBJ_LOADED) == 0))
        return;
    const float *base = Game::Ptr<float>(model, Offsets::OFF_M2MODEL_BASE_MATRIX);
    const float position[3] = {base[12], base[13], base[14]};
    const uint32_t serial = b != nullptr ? b->serial : 0;
    const uint32_t terrainSerial = terrain != nullptr ? Terrain::Serial(*terrain) : 0;
    const uint32_t generation = b != nullptr ? b->generation : 0;
    const bool placed = a.model == model && a.serial == serial && a.terrainSerial == terrainSerial;
    bool moved = true;
    if (placed && a.generation == generation) {
        const float dx = position[0] - a.position[0], dy = position[1] - a.position[1],
                    dz = position[2] - a.position[2];
        moved = !(std::fabs(dx * dx + dy * dy + dz * dz) < EngineFloat(Offsets::VAR_ONE_THOUSANDTH));
    }
    if (!placed) {
        // A new node (0x00670DB0): intensity 1.0, the day ambient.
        a.ambient = a.ambientTarget =
            SunBytes(Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT), Offsets::OFF_M2LIGHT_AMBIENT);
        a.intensity = a.intensityTarget = EngineFloat(Offsets::VAR_ONE);
    }
    if (moved) {
        a.serial = serial;
        a.terrainSerial = terrainSerial;
        a.model = model;
        a.generation = generation;
        std::memcpy(a.position, position, sizeof a.position);
        Place(b, terrain, a, frame, model, base);
        LiquidEnvironment(terrain, a, frame);
    }
    Ease(a, frame, elapsed);
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
        InteriorLight(*b, record, frame, a.ambient, a.directional, direction, a.intensity);
    }
    for (const Light &light : a.lights)
        AddLight(record, light.bytes);
}

namespace {

// The ambience is the actor's placement in this WMO and terrain.
bool Current(const Backdrop *b, const Terrain::Land *terrain, const Ambience &a) {
    return (b != nullptr || terrain != nullptr) && a.model != nullptr && a.serial == (b != nullptr ? b->serial : 0) &&
           a.terrainSerial == (terrain != nullptr ? Terrain::Serial(*terrain) : 0);
}

} // namespace

int Ground(const Backdrop *b, const Terrain::Land *terrain, const Ambience &a) {
    return Current(b, terrain, a) ? a.ground : -1;
}

bool RippleSurface(const Backdrop *b, const Terrain::Land *terrain, const Ambience &a, float &surface) {
    if (!Current(b, terrain, a) || !a.inLiquid || !a.ripples)
        return false;
    surface = a.liquidHeight;
    return true;
}

bool FootstepWet(const Backdrop *b, const Terrain::Land *terrain, const Ambience &a, const float (&point)[3]) {
    if (!Current(b, terrain, a) || !a.inLiquid || !a.splashes)
        return false;
    // 0x0060A7A3: z under the height plus 0.01, then the height less z under
    // 2/3 (a NaN failing both).
    const double z = point[2];
    if (!(static_cast<double>(a.liquidHeight) + EngineFloat(Offsets::VAR_ONE_HUNDREDTH) > z))
        return false;
    return static_cast<double>(a.liquidHeight) - z < EngineFloat(Offsets::VAR_TWO_THIRDS);
}

bool LiquidAt(const Backdrop &b, const float (&point)[3], uint32_t &type, float &surface) {
    using Height_t = bool(__thiscall *)(void *group, const float *point, uint32_t *type, float *height,
                                        float *vector);
    if (Game::Read<uint8_t>(b.object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return false;
    // The group, as a unit's is found (AttachEntityToGround 0x006A8A20): the
    // nearest face down from the point.
    const float end[3] = {point[0], point[1], point[2] - EngineFloat(Offsets::VAR_ONE_THOUSAND)};
    float t = 1.0f;
    uint32_t group = 0;
    uint16_t face = 0;
    if (!Nearest(b, point, end, t, group, face))
        return false;
    void *loaded = LoadedGroup(b.object, group);
    if (loaded == nullptr)
        return false;
    float local[3], height = 0.0f, vector[3] = {};
    TransformPoint(point, b.inverse, local);
    type = Offsets::LIQUID_DRY;
    if (!reinterpret_cast<Height_t>(Offsets::FUN_CMAPOBJGROUP_LIQUID_HEIGHT)(loaded, local, &type, &height, vector) ||
        type == Offsets::LIQUID_DRY)
        return false;
    type &= 3; // the group's liquid word reads as its tile type, as the camera's fog reads it
    // A group whose MOGP liquid word fills it answers FLT_MAX: no surface.
    if (height >= FLT_MAX) {
        surface = FLT_MAX;
        return true;
    }
    const float top[3] = {local[0], local[1], height};
    float scene[3];
    TransformPoint(top, b.placement, scene);
    surface = scene[2];
    return true;
}

bool Trace(const Backdrop &b, const float (&start)[3], const float (&end)[3], bool camera, float &fraction,
           float (&normal)[3]) {
    if (Game::Read<uint8_t>(b.object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return false;
    fraction = 1.0f;
    uint32_t group = 0;
    uint16_t face = 0;
    float local[3] = {0.0f, 0.0f, 0.0f};
    if (!Nearest(b, start, end, fraction, group, face, &local,
                 camera ? Offsets::MAPOBJ_TRACE_MASK_CAMERA : Offsets::MAPOBJ_TRACE_MASK_GROUND))
        return false;
    // Into scene space by the placement's turn: the two ends of the normal
    // through the placement, less each other.
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float base[3], tip[3];
    TransformPoint(origin, b.placement, base);
    TransformPoint(local, b.placement, tip);
    const float direction[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    float dot = 0.0f, length = 0.0f;
    for (int i = 0; i < 3; ++i) {
        normal[i] = tip[i] - base[i];
        dot += normal[i] * direction[i];
        length += normal[i] * normal[i];
    }
    // The faces are hit from either side: the normal faces the start.
    length = std::sqrt(length);
    const float k = length > 0.0f ? (dot > 0.0f ? -1.0f : 1.0f) / length : 0.0f;
    for (float &c : normal)
        c *= k;
    return true;
}

namespace {

// The box in the object's space as the world's map object tests take it
// (TestMapObjectBox 0x006AD330, GatherMapObjectFacets 0x006AAAB0): the
// centre through the inverse, the half extents through its axes. False when
// it misses the object (BoxOverlaps).
bool LocalBox(const Backdrop &b, const float (&box)[6], float (&local)[6]) {
    using RotateBox_t = void(__fastcall *)(const float *axes, const float *box, float *out);
    using Overlaps_t = bool(__thiscall *)(void *object, const float *box);
    float centre[3], half[6];
    for (int i = 0; i < 3; ++i)
        centre[i] = (box[i] + box[i + 3]) * 0.5f;
    for (int i = 0; i < 6; ++i)
        half[i] = box[i] - centre[i % 3];
    float moved[3];
    TransformPoint(centre, b.inverse, moved);
    const float *m = b.inverse;
    const float axes[9] = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
    reinterpret_cast<RotateBox_t>(Offsets::FUN_ROTATE_BOX)(axes, half, local);
    for (int i = 0; i < 6; ++i)
        local[i] += moved[i % 3];
    return reinterpret_cast<Overlaps_t>(Offsets::FUN_CMAPOBJ_BOX_OVERLAPS)(b.object, local);
}

// TestMapObjectBox (0x006AD330) for the one WMO: the facets of the groups
// the box touches into the batches.
bool CollectBox(const Backdrop &b, const float (&box)[6], uint32_t mask) {
    using Collect_t = bool(__thiscall *)(void *object, void *out, const float *box, const void *def, uint32_t mask);
    float local[6];
    if (!LocalBox(b, box, local))
        return false;
    uint32_t out = 0;
    return reinterpret_cast<Collect_t>(Offsets::FUN_CMAPOBJ_COLLECT_BOX_FACETS)(b.object, &out, local, b.def, mask);
}

// ---- the sweep ------------------------------------------------------------------------
//
// The scene's bodies meet the WMO as the client's mover meets the world:
// SceneContact.cpp is its contact sweep, and this is its world query
// (0x006721B0) for the one WMO. CollectBox with the mover's class fills the
// facet batches, and each batch's triangles become facets as the facet
// emitter (0x00671CC0) makes them: the vertices through the batch's matrix
// (the same arithmetic as its 0x007BCA80), the plane from their winding. The
// client's own lists (0x00C4E52C) and cached box are never touched.

} // namespace

namespace {

// Each batch triangle's vertices through the batch's matrix (0x007BCA80).
template <typename Add> void ForEachBatchTriangle(Add add) {
    const uint32_t batches = Game::Read<uint32_t>(Offsets::VAR_FACET_BATCH_COUNT);
    for (uint32_t i = 0; i < batches; ++i) {
        const uint8_t *batch = FacetBatch(i);
        const auto *m = Game::Read<const Matrix *>(batch, Offsets::OFF_FACET_BATCH_MATRIX);
        const auto *vertex = Game::Read<const float *>(batch, Offsets::OFF_FACET_BATCH_VERTICES);
        const auto *index = Game::Read<const uint16_t *>(batch, Offsets::OFF_FACET_BATCH_INDICES);
        const uint16_t triangles = Game::Read<uint16_t>(batch, Offsets::OFF_FACET_BATCH_TRIANGLE_COUNT);
        if (m == nullptr || vertex == nullptr || index == nullptr)
            continue;
        for (uint32_t t = 0; t < triangles; ++t, index += 3) {
            float v[3][3];
            for (int c = 0; c < 3; ++c) {
                const float *p = vertex + 3 * index[c];
                const float in[3] = {p[0], p[1], p[2]};
                TransformPoint(in, *m, v[c]);
            }
            add(v);
        }
    }
}

} // namespace

void EmitFacetBatches(std::vector<Contact::Facet> &out) {
    ForEachBatchTriangle([&](const float (&v)[3][3]) { out.push_back(Contact::MakeFacet(v[0], v[1], v[2])); });
}

namespace {

// Whether two boxes touch, closed, a NaN missing (0x006ACB40 and 0x00699330,
// the doodad gather's own test at 0x006ABCEE).
bool Touches(const float (&a)[6], const float (&box)[6]) {
    for (int i = 0; i < 3; ++i)
        if (!(a[i] <= box[i + 3] && box[i] <= a[i + 3]))
            return false;
    return true;
}

// CM2Model::GatherCollisionFacets (0x007137C0): the model's collision mesh
// through the def's matrix, one facet per triangle whose three vertices are
// not all past the same face of the box, its plane from the mesh's own
// normal turned by the matrix's normalized axes. The client caches the
// moved vertices on the model; they are made here for each call.
void AddDoodadFacets(const Doodad &d, const float (&box)[6], std::vector<Contact::Facet> &out) {
    using D = double;
    const auto *data = Game::Read<const uint8_t *>(Game::Read<const uint8_t *>(d.model, Offsets::OFF_M2MODEL_SHARED),
                                                   Offsets::OFF_M2SHARED_DATA);
    const uint32_t vertexCount = Game::Read<uint32_t>(data, Offsets::OFF_M2DATA_COLLISION_VERTEX_COUNT);
    const auto *vertices = Game::Read<const float *>(data, Offsets::OFF_M2DATA_COLLISION_VERTICES);
    std::vector<std::array<float, 3>> moved(vertexCount);
    std::vector<uint32_t> outside(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        const float in[3] = {vertices[3 * i], vertices[3 * i + 1], vertices[3 * i + 2]};
        float v[3];
        TransformPoint(in, d.matrix, v);
        moved[i] = {v[0], v[1], v[2]};
        uint32_t code = 0;
        for (int k = 0; k < 3; ++k) {
            if (v[k] < box[k])
                code |= 1u << (2 * k);
            else if (v[k] > box[k + 3])
                code |= 2u << (2 * k);
        }
        outside[i] = code;
    }
    // The matrix's rows, each scaled to unit length unless shorter than
    // 2^-22 (0x00713996).
    float axes[3][3] = {{d.matrix[0], d.matrix[1], d.matrix[2]},
                        {d.matrix[4], d.matrix[5], d.matrix[6]},
                        {d.matrix[8], d.matrix[9], d.matrix[10]}};
    for (auto &a : axes) {
        const double length = std::sqrt((D(a[0]) * a[0] + D(a[1]) * a[1]) + D(a[2]) * a[2]);
        if (!(std::fabs(length) < 2.384185791015625e-7)) {
            const double inverse = 1.0 / length;
            for (float &c : a)
                c = static_cast<float>(c * inverse);
        }
    }
    const uint32_t triangles = Game::Read<uint32_t>(data, Offsets::OFF_M2DATA_COLLISION_TRIANGLE_COUNT);
    const auto *index = Game::Read<const uint16_t *>(data, Offsets::OFF_M2DATA_COLLISION_INDICES);
    const auto *normals = Game::Read<const float *>(data, Offsets::OFF_M2DATA_COLLISION_NORMALS);
    for (uint32_t t = 0; t < triangles; ++t, index += 3) {
        if (outside[index[0]] & outside[index[1]] & outside[index[2]])
            continue;
        Contact::Facet f;
        for (int c = 0; c < 3; ++c)
            std::memcpy(f.v[c], moved[index[c]].data(), sizeof f.v[c]);
        const float *n = normals + 3 * t;
        for (int k = 0; k < 3; ++k)
            f.plane.n[k] = static_cast<float>((D(axes[2][k]) * n[2] + D(axes[1][k]) * n[1]) + D(axes[0][k]) * n[0]);
        f.plane.d = static_cast<float>(
            -((D(f.v[0][2]) * f.plane.n[2] + D(f.v[0][1]) * f.plane.n[1]) + D(f.plane.n[0]) * f.v[0][0]));
        out.push_back(f);
    }
}

// The world query (0x006721B0) for the one WMO: GatherMapObjectFacets
// (0x006AAAB0) under the mover's class. The placement's box must touch the
// query's; then the groups' faces, then each group's box that touches it
// gives its doodads (GatherDoodadFacets 0x006ABC40), each once per query.
// A group not yet parsed, or a doodad whose model file has not loaded, fails
// the query (the client waits for them), with the facets gathered so far
// left in the list as the client leaves them.
bool QueryFacetsIn(Backdrop &b, const float (&box)[6], std::vector<Contact::Facet> &out) {
    using Bounds_t = void(__thiscall *)(void *object, float *out);
    using Info_t = const uint8_t *(__thiscall *)(void *object, uint32_t group);
    out.clear();
    void *object = b.object;
    if (Game::Read<uint8_t>(object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return false;
    ++b.queryId;
    float bounds[6], placed[6];
    reinterpret_cast<Bounds_t>(Offsets::FUN_CMAPOBJ_GET_BOUNDING_BOX)(object, bounds);
    TransformBox(b.placement, bounds, placed);
    if (!Touches(placed, box))
        return true;
    ResetFacetBatches();
    if (CollectBox(b, box, Offsets::WORLD_TRACE_CLASS_LOS))
        EmitFacetBatches(out);
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    for (uint32_t g = 0; g < count; ++g) {
        const uint8_t *info = reinterpret_cast<Info_t>(Offsets::FUN_CMAPOBJ_GET_GROUP_INFO)(object, g);
        if (info == nullptr)
            continue;
        float local[6], groupBox[6];
        std::memcpy(local, info + Offsets::OFF_MOGI_BOUNDS, sizeof local);
        TransformBox(b.placement, local, groupBox);
        if (!Touches(groupBox, box))
            continue;
        // CMapObj::GetGroup (0x006A42B0): the group parsed, and here its
        // doodads made, as the client makes them when it parses the group.
        if (!IsParsed(Group(object, g)) || g >= b.groupsMade.size() || !b.groupsMade[g])
            return false;
        for (Doodad *d : b.groupDoodads[g]) {
            if (d->stamp == b.queryId || d->model == nullptr)
                continue;
            if (!d->bounded)
                return false;
            if (Touches(d->collision, box))
                AddDoodadFacets(*d, box, out);
            d->stamp = b.queryId;
        }
    }
    return true;
}

// The swimmer's liquid query for the one WMO: the world gather's liquid leg
// (0x006AAAB0 under 0xF0000) is CollectBoxLiquid (0x006A3C20), which takes
// each loaded group whose bounds touch the box (closed, a NaN missing) and
// emits that group's liquid tiles on their own (0x006BBCB0).
bool QueryLiquidIn(const Backdrop &b, const float (&box)[6], std::vector<Contact::Facet> &out) {
    using Info_t = const uint8_t *(__thiscall *)(void *object, uint32_t group);
    using CollectLiquid_t =
        bool(__thiscall *)(void *group, uint32_t *flags, const float *box, const void *def, uint32_t mask);
    out.clear();
    if (Game::Read<uint8_t>(b.object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return false;
    float local[6];
    if (!LocalBox(b, box, local))
        return true;
    const uint32_t count = Game::Read<uint32_t>(b.object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    for (uint32_t g = 0; g < count; ++g) {
        const uint8_t *info = reinterpret_cast<Info_t>(Offsets::FUN_CMAPOBJ_GET_GROUP_INFO)(b.object, g);
        if (info == nullptr)
            continue;
        const float *bounds = reinterpret_cast<const float *>(info + Offsets::OFF_MOGI_BOUNDS);
        bool touches = true;
        for (int i = 0; i < 3; ++i)
            if (!(bounds[i] <= local[i + 3] && local[i] <= bounds[i + 3]))
                touches = false;
        if (!touches)
            continue;
        void *group = LoadedGroup(b.object, g);
        if (group == nullptr)
            continue;
        ResetFacetBatches();
        uint32_t flags = 0;
        reinterpret_cast<CollectLiquid_t>(Offsets::FUN_CMAPOBJGROUP_COLLECT_LIQUID_BOX)(
            group, &flags, local, b.def, Offsets::WORLD_CLASS_SWIM_LIQUID);
        EmitFacetBatches(out);
    }
    return true;
}

} // namespace

bool QueryFacets(void *backdrop, const float (&box)[6], std::vector<Contact::Facet> &out) {
    if (backdrop == nullptr) {
        out.clear();
        return false;
    }
    return QueryFacetsIn(*static_cast<Backdrop *>(backdrop), box, out);
}

bool QueryLiquidFacets(void *backdrop, const float (&box)[6], std::vector<Contact::Facet> &out) {
    if (backdrop == nullptr) {
        out.clear();
        return false;
    }
    return QueryLiquidIn(*static_cast<const Backdrop *>(backdrop), box, out);
}


namespace {

// A texture matrix built for absolute positions, for positions relative to
// the eye (what texgen gives, the vertices being relative to it):
// Translate(eye) x m.
void FromEye(Matrix &m, const float (&eye)[3]) {
    for (int j = 0; j < 4; ++j)
        m[12 + j] += eye[0] * m[j] + eye[1] * m[4 + j] + eye[2] * m[8 + j];
}

// DrawDecalBatches' (0x006D7480) states around `draw(color, lighting)`: the
// ramp on stage 1, the depth bias, texgen from the position through the
// texture matrices, and the vertex colour (the material instead while
// lighting is on; red and blue swapped where the device wants them).
template <typename Draw> void WithDecalStates(uint32_t argb, Draw draw) {
    using Caps_t = const uint8_t *(__fastcall *)();
    Gx::PushStates();
    const int blend = Gx::GetState(Offsets::GXRS_BLEND);
    if (blend == Offsets::GXRS_BLEND_MODULATE) {
        Gx::SetState(Offsets::GXRS_TEXTURE0 + 1, Game::Read<void *>(Offsets::VAR_SHADOW_GREY_RAMP));
        Gx::SetState(Offsets::GXRS_COMBINER0 + 1, Offsets::GXRS_COMBINE_ADD);
    } else {
        Gx::SetState(Offsets::GXRS_TEXTURE0 + 1, Game::Read<void *>(Offsets::VAR_SHADOW_RAMP));
    }
    const float bias = Game::Read<float>(Offsets::VAR_SHADOW_BIAS);
    if (bias != 0.0f)
        Gx::SetStateFloat(Offsets::GXRS_DEPTH_BIAS, (bias + bias) * Game::Read<float>(Offsets::VAR_SHADOW_BIAS_SCALE));
    for (int unit = 0; unit < 2; ++unit) {
        Gx::SetState(Offsets::GXRS_TEXGEN0 + unit, Offsets::GXRS_TEXGEN_WORLD_POS);
        Gx::SetState(Offsets::GXRS_TEXXFORM0 + unit, Offsets::GXRS_TEXXFORM_MATRIX);
    }
    uint32_t color = argb;
    const int lighting = Gx::GetState(Offsets::GXRS_LIGHTING);
    if (lighting == 1) {
        Gx::SetStateWord(Offsets::GXRS_MATERIAL, argb);
    } else if (Game::Read<int>(reinterpret_cast<Caps_t>(Offsets::FUN_GX_DEVICE_CAPS)(),
                               Offsets::OFF_GXCAPS_COLOR_SWAP) == 1) {
        color = (argb & 0xFF00FF00u) | (argb & 0xFFu) << 16 | (argb >> 16 & 0xFFu);
    }
    draw(color, lighting);
    Gx::PopStates();
}

// The facet batches CollectBox filled, drawn with the decal states set.
void DrawFacetBatches(uint32_t color, int lighting, const float (&eye)[3]) {
    using WriteColored_t = void(__fastcall *)(const void *batch);
    using Write_t = void(__thiscall *)(const void *batch, uint32_t color);
    using Indices_t = void(__fastcall *)(const void *batch);
    struct Prim {
        uint32_t type, base;
        uint16_t count, min, max, unused;
    };
    using Render_t = void(__fastcall *)(const Prim *prim, int count);
    Matrix toEye;
    Identity(toEye);
    Translate(toEye, {-eye[0], -eye[1], -eye[2]});
    Gx::PushTransform(Offsets::GX_XFORM_WORLD);
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
        Gx::SetTransform(Offsets::GX_XFORM_WORLD, world);
        if (lighting == 1)
            reinterpret_cast<WriteColored_t>(Offsets::FUN_WRITE_DECAL_VERTICES_COLOURED)(batch);
        else
            reinterpret_cast<Write_t>(Offsets::FUN_WRITE_DECAL_VERTICES)(batch, color);
        reinterpret_cast<Indices_t>(Offsets::FUN_WRITE_DECAL_INDICES)(batch);
        const Prim prim{Offsets::GX_PRIM_TRIANGLES, 0, indices, 0, static_cast<uint16_t>(indices - 1), 0};
        reinterpret_cast<Render_t>(Offsets::FUN_GX_PRIM_RENDER_ONE)(&prim, 1);
    }
    Gx::PopTransform(Offsets::GX_XFORM_WORLD);
}

} // namespace

void LiquidTriangles(const Backdrop *b, const Terrain::Land *terrain, const float (&box)[6], std::vector<float> &out) {
    // EmitRipple (0x0068F6E1): the batches reset, TestWorldBox with the
    // liquid classes, then each batch's triangles.
    ResetFacetBatches();
    bool any = b != nullptr && CollectBox(*b, box, Offsets::LIQUID_TRIANGLE_CLASS);
    if (terrain != nullptr)
        any = Terrain::CollectDecalBatches(*terrain, box, Offsets::LIQUID_TRIANGLE_CLASS) || any;
    if (!any)
        return;
    ForEachBatchTriangle([&](const float (&v)[3][3]) {
        for (const auto &p : v)
            out.insert(out.end(), p, p + 3);
    });
}

namespace {

// DrawDecalBatches (0x006D7480) over the facets of the WMO and the terrain,
// as TestWorldBox (0x006AD2C0) collects map objects then terrain, the eye for
// the world camera.
void DrawBatches(const Backdrop *b, const Terrain::Land *terrain, const float (&box)[6], uint32_t argb,
                 uint32_t mask, const float (&eye)[3]) {
    ResetFacetBatches();
    bool any = b != nullptr && CollectBox(*b, box, mask);
    if (terrain != nullptr && (mask & Offsets::DECAL_MASK_TERRAIN) != 0)
        any = Terrain::CollectDecalBatches(*terrain, box, mask) || any;
    if (!any)
        return;
    WithDecalStates(argb, [&](uint32_t color, int lighting) { DrawFacetBatches(color, lighting, eye); });
}

// The texture transforms DrawDecal (0x006D7330) builds for absolute
// positions (the other mode reads the world's camera), moved to the eye,
// pushed around `draw()`.
template <typename Draw>
void WithDecalTransforms(const float (&box)[6], const float (&m)[16], float fade, const float (&eye)[3], Draw draw) {
    using Build_t = void(__fastcall *)(float *tex0, float *tex1, const float *box, const float *m, float fade,
                                       int absolute);
    Matrix tex0, tex1;
    Identity(tex0);
    Identity(tex1);
    reinterpret_cast<Build_t>(Offsets::FUN_BUILD_DECAL_TRANSFORMS)(tex0, tex1, box, m, fade, 1);
    FromEye(tex0, eye);
    FromEye(tex1, eye);
    Gx::PushTransform(0, tex0);
    Gx::PushTransform(1, tex1);
    draw();
    Gx::PopTransform(0);
    Gx::PopTransform(1);
}

} // namespace

// DrawDecal (0x006D7330) onto the WMO and the terrain.
void DrawDecal(const Backdrop *b, const Terrain::Land *terrain, const float (&box)[6], uint32_t argb,
               const float (&m)[16], float fade, uint32_t mask, const float (&eye)[3]) {
    if (b != nullptr && Game::Read<uint8_t>(b->object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        b = nullptr;
    if (b == nullptr && terrain == nullptr)
        return;
    WithDecalTransforms(box, m, fade, eye, [&] { DrawBatches(b, terrain, box, argb, mask, eye); });
}

// The flat ground stands in for the world's faces: one quad over the box at
// `z`, texgen placing the texture on it as on any face.
void DrawDecalOnPlane(const float (&box)[6], uint32_t argb, const float (&m)[16], float fade, float z,
                      const float (&eye)[3]) {
    using PrimStreams_t = void(__fastcall *)(int count, const void *pos, int posStride, const void *normal,
                                             int normalStride, const void *colors, int colorStride, int unused8,
                                             int unused9, const void *uv0, int uv0Stride, const void *uv1,
                                             int uv1Stride);
    using PrimIndexed_t = void(__fastcall *)(int primType, int indexCount, const uint16_t *indices);
    WithDecalTransforms(box, m, fade, eye, [&] {
        WithDecalStates(argb, [&](uint32_t color, int) {
            const float corners[4][2] = {{box[0], box[1]}, {box[3], box[1]}, {box[0], box[4]}, {box[3], box[4]}};
            float pos[4][3];
            for (int i = 0; i < 4; ++i) {
                pos[i][0] = corners[i][0] - eye[0];
                pos[i][1] = corners[i][1] - eye[1];
                pos[i][2] = z - eye[2];
            }
            const uint32_t colors[4] = {color, color, color, color};
            static const uint16_t kStrip[4] = {0, 1, 2, 3};
            reinterpret_cast<PrimStreams_t>(Offsets::FUN_GX_PRIM_STREAMS)(4, pos, 12, nullptr, 0, colors, 4, 0, 0,
                                                                         nullptr, 0, nullptr, 0);
            reinterpret_cast<PrimIndexed_t>(Offsets::FUN_GX_PRIM_INDEXED)(Offsets::GX_PRIM_TRIANGLE_STRIP, 4,
                                                                          kStrip);
        });
    });
}

// The world draws its entity decals in the fog CWorld::SetFogState set for
// the whole pass (RenderWorldFrame 0x0048361D); the selection circle's state
// (0x00614E00) turns lighting off, not fog. The scene's paint holds no fog of
// its own at that point (the device's 0 to 1 in 0xFFFF0080), so the scene's
// stands in: the WMO's record, as its liquids get their fog, or without a WMO
// the frame's light and fog, as an actor gets them.
void CommitDecalFog(const Backdrop *b, void *frame, const float (&point)[3], const float (&eye)[3]) {
    if (b != nullptr) {
        CommitLighting(*b, frame, eye);
        return;
    }
    alignas(16) uint8_t record[Offsets::M2_LIGHTING_RECORD_SIZE];
    const float sphere[4] = {point[0], point[1], point[2], 0.0f};
    reinterpret_cast<void(__thiscall *)(void *, void *, const float *)>(Offsets::FUN_M2LIGHTING_INIT)(record, nullptr,
                                                                                                     sphere);
    FrameLightAndFog(record, frame);
    using Commit_t = void(__thiscall *)(void *record, const float *origin, int fog);
    reinterpret_cast<Commit_t>(Offsets::FUN_M2LIGHTING_SET_DEVICE_LIGHTS)(record, eye, 1);
}

void Update(Backdrop &b, void *frame, bool sounds, float elapsed) {
    const View view = CameraView(frame);
    b.farClip = view.farClip;
    b.sounds = sounds && Game::Read<uint8_t>(Offsets::VAR_IN_WORLD) != 0;
    if (!b.sounds)
        Silence(b);
    void *object = b.object;
    if (Game::Read<uint8_t>(object, Offsets::OFF_CMAPOBJ_LOADED) == 0) {
        b.cameraLiquid = OutdoorCameraLiquid(frame);
        return;
    }
    UpdateCameraFog(b, frame, view.eye, view.farClip, elapsed);
    const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
    if (b.groupsMade.empty()) {
        b.groupsMade.assign(count, false);
        b.groupLights.assign(count, {});
        b.byIndex.assign(Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_DOODAD_COUNT), nullptr);
        b.groupDoodads.assign(count, {});
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
    ShowDoodads(b.doodads, view);
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
    // The render states a pass of the WMO sets (CommitLighting's fog range)
    // last until it ends.
    const Gx::SavedStates states;
    PushObjectSpace(b, eye);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_PUSH)();
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_FRUSTUM_STORE)(volume);
    reinterpret_cast<void(__fastcall *)(const float *)>(Offsets::FUN_FRUSTUM_TRANSFORM)(b.inverse);
    CommitLighting(b, frame, eye);
    DrawGroups(b, frame);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_POP)();
    PopObjectSpace();
}

uint32_t CameraLiquid(const Backdrop &b) { return b.cameraLiquid; }

uint32_t MapOf(const Backdrop &b) { return b.mapID; }

namespace {

using Record = uint8_t[Offsets::LIGHT_RECORD_SIZE];

const uint8_t *LightParams(const uint8_t *light, uint32_t reference) {
    return reinterpret_cast<const uint8_t *(__fastcall *)(const uint8_t *, uint32_t)>(Offsets::FUN_GET_LIGHT_PARAMS)(
        light, reference);
}

// GatherLightRecord into a fresh record (its members zero themselves).
void Gather(int h, Record &out, const uint8_t *params) {
    std::memset(out, 0, sizeof out);
    reinterpret_cast<void(__fastcall *)(int, uint8_t *, const uint8_t *)>(Offsets::FUN_GATHER_LIGHT_RECORD)(h, out,
                                                                                                          params);
}

// BuildLightTable (0x006D2260) and BlendAreaLights (0x006D2D00) for the
// backdrop's map at the scene's camera, as the world builds its table at its
// camera: the time of day is the world's; the storm blend, which follows the
// weather where the player is, is left out. False when the map has no light.
bool BuildMapLight(uint32_t mapID, uint32_t cameraLiquid, const float (&eye)[3], Record &table) {
    const float day = EngineFloat(Offsets::VAR_LIGHT_TIME_OF_DAY) * EngineFloat(Offsets::VAR_HALF_MINUTES_PER_DAY);
    const int h = static_cast<int>(std::lrint(static_cast<double>(day) - EngineFloat(Offsets::VAR_DAY_NIGHT_HALF)));
    const uint32_t liquid = cameraLiquid == Offsets::LIQUID_DRY ? cameraLiquid : (cameraLiquid & 3);
    if (liquid == Offsets::LIQUID_MAGMA || liquid == Offsets::LIQUID_SLIME) {
        const int id = liquid == Offsets::LIQUID_MAGMA ? Offsets::LIGHTPARAMS_MAGMA : Offsets::LIGHTPARAMS_SLIME;
        if (Game::Read<int32_t>(Offsets::VAR_LIGHTPARAMS_LARGEST_ID) < id)
            return false;
        const auto *params = Game::Read<const uint8_t *const *>(Offsets::VAR_LIGHTPARAMS_ID_MAP)[id];
        if (params == nullptr)
            return false;
        Gather(h, table, params);
        return true;
    }
    const uint32_t reference = liquid == Offsets::LIQUID_DRY ? 0 : 1;
    // The light list (0x006D6170): slot 0 the last row at the origin, else
    // Light id 1; the rest in table order.
    const auto *rows = Game::Read<const uint8_t *>(Offsets::VAR_LIGHT_ROWS);
    const auto count = Game::Read<uint32_t>(Offsets::VAR_LIGHT_ROW_COUNT);
    const uint8_t *origin = nullptr;
    std::vector<const uint8_t *> area;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *row = rows + i * Offsets::LIGHT_ROW_SIZE;
        if (Game::Read<uint32_t>(row, Offsets::OFF_LIGHT_MAP) != mapID)
            continue;
        const float *p = Game::Ptr<float>(row, Offsets::OFF_LIGHT_POSITION);
        if (p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f)
            origin = row;
        else
            area.push_back(row);
    }
    if (origin == nullptr && Game::Read<int32_t>(Offsets::VAR_LIGHT_LARGEST_ID) >= 1)
        origin = Game::Read<const uint8_t *const *>(Offsets::VAR_LIGHT_ID_MAP)[1];
    const uint8_t *base = origin != nullptr ? LightParams(origin, reference) : nullptr;
    if (base == nullptr)
        return false;
    Gather(h, table, base);
    // The lights around the eye, farthest first, the nearest blended last.
    std::vector<std::pair<float, const uint8_t *>> near;
    for (const uint8_t *row : area) {
        const float *p = Game::Ptr<float>(row, Offsets::OFF_LIGHT_POSITION);
        const float dx = eye[0] - p[0], dy = eye[1] - p[1], dz = eye[2] - p[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < Game::Read<float>(row, Offsets::OFF_LIGHT_OUTER_RADIUS))
            near.emplace_back(d, row);
    }
    std::stable_sort(near.begin(), near.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
    for (const auto &[d, row] : near) {
        const uint8_t *params = LightParams(row, reference);
        if (params == nullptr)
            continue;
        Record light;
        Gather(h, light, params);
        const float inner = Game::Read<float>(row, Offsets::OFF_LIGHT_INNER_RADIUS);
        const float outer = Game::Read<float>(row, Offsets::OFF_LIGHT_OUTER_RADIUS);
        const float w = inner < d ? 1.0f - (d - inner) / (outer - inner) : 1.0f;
        reinterpret_cast<void(__fastcall *)(uint8_t *, const uint8_t *, float)>(Offsets::FUN_OVERBLEND_LIGHT_RECORD)(
            table, light, w);
    }
    return true;
}

// BuildSceneFog (0x006CEE30) over map `mapID`'s light table for the camera's
// liquid: the end capped at the far clip (a NaN takes it), the start the
// scale of it, colour band 7. False while dry, for kNoMap or a map without
// a light.
bool MapLiquidFog(uint32_t mapID, uint32_t cameraLiquid, const float (&eye)[3], float farClip, FogValues &out) {
    Record table;
    if (cameraLiquid == Offsets::LIQUID_DRY || mapID == kNoMap || !BuildMapLight(mapID, cameraLiquid, eye, table))
        return false;
    out.end = Game::Read<float>(table, Offsets::OFF_LIGHT_RECORD_FOG_END);
    if (!(out.end < farClip))
        out.end = farClip;
    out.start = Game::Read<float>(table, Offsets::OFF_LIGHT_RECORD_FOG_START_SCALE) * out.end;
    out.color = Game::Read<uint32_t>(table, Offsets::OFF_LIGHT_RECORD_FOG_COLOR);
    return true;
}

} // namespace

namespace {
constexpr uintptr_t kWaterFields[] = {
    Offsets::OFF_LIGHT_RECORD_SHALLOW_OCEAN,      Offsets::OFF_LIGHT_RECORD_DEEP_OCEAN,
    Offsets::OFF_LIGHT_RECORD_SHALLOW_RIVER,      Offsets::OFF_LIGHT_RECORD_DEEP_RIVER,
    Offsets::OFF_LIGHT_RECORD_RIVER_ALPHA_SHALLOW, Offsets::OFF_LIGHT_RECORD_RIVER_ALPHA_DEEP,
    Offsets::OFF_LIGHT_RECORD_OCEAN_ALPHA_SHALLOW, Offsets::OFF_LIGHT_RECORD_OCEAN_ALPHA_DEEP};
static_assert(std::size(kWaterFields) == MapWater::kFields, "one saved word per water field");
uint8_t *LiveLightTable() { return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(Offsets::VAR_LIGHT_TABLE)); }
} // namespace

MapWater::MapWater(uint32_t mapID, uint32_t cameraLiquid, const float (&eye)[3]) {
    Record table;
    if (mapID == kNoMap || !BuildMapLight(mapID, cameraLiquid, eye, table))
        return;
    for (size_t i = 0; i < std::size(kWaterFields); ++i) {
        std::memcpy(&saved_[i], LiveLightTable() + kWaterFields[i], sizeof saved_[i]);
        std::memcpy(LiveLightTable() + kWaterFields[i], table + kWaterFields[i], sizeof saved_[i]);
    }
    swapped_ = true;
}

MapWater::~MapWater() {
    if (!swapped_)
        return;
    for (size_t i = 0; i < std::size(kWaterFields); ++i)
        std::memcpy(LiveLightTable() + kWaterFields[i], &saved_[i], sizeof saved_[i]);
}

LiquidFog::LiquidFog(void *frame, uint32_t mapID, uint32_t cameraLiquid, const float (&eye)[3], float farClip) {
    FogValues fog;
    if (!MapLiquidFog(mapID, cameraLiquid, eye, farClip, fog))
        return;
    std::memcpy(saved_, Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS), sizeof saved_);
    frame_ = frame;
    Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) |= Offsets::SIMPLEMODEL_FOG_ARMED;
    Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR) = fog.color;
    Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR) = fog.start;
    Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) = fog.end;
}

LiquidFog::~LiquidFog() {
    if (frame_ != nullptr)
        std::memcpy(Game::Ptr<uint8_t>(frame_, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS), saved_, sizeof saved_);
}

// DrawGroupLiquidPass (0x00684CD0) over the one WMO's parsed liquid groups.
// The world commits a record per group at the group def's sphere; here the
// WMO's one record (the drawn fog, the lights near the WMO) serves them all.
// Magma and slime draw unlit and water takes its colours from the light
// table, so the record gives them their fog.
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
    // Outdoor water takes the light of the backdrop's map at the camera.
    const MapWater mapLight(b.mapID, b.cameraLiquid, eye);
    const Gx::SavedStates states;
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

// ---- the sky --------------------------------------------------------------------------
//
// The world clears its frame and draws its sky before its terrain and map
// objects (DrawWorldScene 0x00681070). With no interior group holding the
// camera and the camera dry, it clears to black and runs DrawSky
// (0x006D4940) in its view, which has no translation. Under liquid it clears
// to the zone fog's colour, and from inside to the drawn fog's, without the
// sky. The scene's view has no translation either, so the scene runs the
// same pass.
//
// The pass draws the world's sky objects, which the world's light build
// (0x006D4530, each world frame) makes from the world's camera and light.
// The parts that come from them are made again for the scene, as the build
// makes them: the dome's colours from the scene camera's heading, and, for a
// backdrop in a map, from that map's light at the scene's camera, with the
// sun disc and white moon colour and the skybox slots of that light. The
// world's are put back after the pass. The cloud texture stays the world's
// (the world builds it a band at a time from its own light); the stars and
// the bodies' places follow only the time of day.
//
// From inside, the world draws the sky only when its portal walk from the
// camera's groups reaches an exterior group (WalkPortals 0x006B41C0); a walk
// that meets a group flagged for a skybox puts the WMO's own skybox (MOSB)
// into slot 0 at the camera's WMO fog weight. The scene runs the same walks,
// with the second walk from the exterior groups the first walk's windows
// show. The sun and moon glares (0x006D48C0) are not drawn.

namespace {

uint8_t *LightState() { return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(Offsets::VAR_LIGHT_STATE)); }

// The light state's heading (0x006D4530): atan2(y, x) of the camera's
// forward, or atan2(z, x) when its x-y length is short, in [0, 2pi).
float Heading(const float (&forward)[3]) {
    const double x = forward[0], y = forward[1];
    const bool level = y * y + x * x > Game::Read<double>(Offsets::VAR_HEADING_MIN_XY);
    float heading = static_cast<float>(level ? std::atan2(y, x) : std::atan2(static_cast<double>(forward[2]), x));
    if (heading < 0.0f)
        heading = static_cast<float>(static_cast<double>(heading) + EngineFloat(Offsets::VAR_HEADING_TWO_PI));
    return heading;
}

// The skybox slots of a light record (0x006D2C79): each pair with an id, a
// weight above 0 and an id below the LightSkybox count interns its path.
void FillSkyboxSlots(const Record &table, uint8_t *state) {
    using Intern_t = void *(__fastcall *)(const char *path);
    const auto count = Game::Read<int32_t>(Offsets::VAR_LIGHTSKYBOX_COUNT);
    const auto largest = Game::Read<int32_t>(Offsets::VAR_LIGHTSKYBOX_LARGEST_ID);
    const auto *rows = Game::Read<const uint8_t *const *>(Offsets::VAR_LIGHTSKYBOX_ID_MAP);
    for (uint32_t k = 0; k < Offsets::LIGHT_SKYBOX_SLOTS; ++k) {
        const int32_t id = Game::Read<int32_t>(table, Offsets::OFF_LIGHT_RECORD_SKYBOXES + k * 8);
        const float weight = Game::Read<float>(table, Offsets::OFF_LIGHT_RECORD_SKYBOXES + k * 8 + 4);
        if (id == 0 || !(weight > 0.0f) || !(id < count))
            continue;
        const uint8_t *row = id < 0 || largest < id ? nullptr : rows[id];
        if (row == nullptr)
            continue; // the engine reads the null row's path
        Game::Ref<void *>(state, Offsets::OFF_LIGHT_STATE_SKYBOXES + k * 4) =
            reinterpret_cast<Intern_t>(Offsets::FUN_INTERN_SKYBOX)(
                Game::Read<const char *>(row, Offsets::OFF_LIGHTSKYBOX_PATH));
        Game::Ref<float>(state, Offsets::OFF_LIGHT_STATE_SKYBOX_WEIGHTS + k * 4) = weight;
    }
}

// The sky objects the scene's pass makes again, kept to be put back.
class SkyObjects {
  public:
    SkyObjects() {
        uint8_t *state = LightState();
        std::memcpy(table_, Table(), sizeof table_);
        std::memcpy(slots_, state + Offsets::OFF_LIGHT_STATE_OVERRIDE_SKYBOX, sizeof slots_);
        heading_ = Game::Read<float>(state, Offsets::OFF_LIGHT_STATE_HEADING);
        tint_ = Game::Read<uint8_t>(state, Offsets::OFF_LIGHT_STATE_TINT_LEVEL);
        for (uint32_t i = 0; i < Offsets::SKY_DISCS_LIT; ++i)
            discs_[i] = Game::Read<uint32_t>(Disc(i));
        const uint32_t *colours = DomeColours();
        domeColours_.assign(colours, colours + DomeColourCount());
    }
    ~SkyObjects() {
        uint8_t *state = LightState();
        std::memcpy(Table(), table_, sizeof table_);
        std::memcpy(state + Offsets::OFF_LIGHT_STATE_OVERRIDE_SKYBOX, slots_, sizeof slots_);
        Game::Ref<float>(state, Offsets::OFF_LIGHT_STATE_HEADING) = heading_;
        Game::Ref<uint8_t>(state, Offsets::OFF_LIGHT_STATE_TINT_LEVEL) = tint_;
        for (uint32_t i = 0; i < Offsets::SKY_DISCS_LIT; ++i)
            Game::Ref<uint32_t>(Disc(i)) = discs_[i];
        std::memcpy(DomeColours(), domeColours_.data(), domeColours_.size() * sizeof(uint32_t));
    }
    SkyObjects(const SkyObjects &) = delete;
    SkyObjects &operator=(const SkyObjects &) = delete;

    static uint8_t *Table() { return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(Offsets::VAR_LIGHT_TABLE)); }
    static uint8_t *Dome() { return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(Offsets::VAR_SKY_DOME)); }
    static uint32_t *DomeColours() { return Game::Read<uint32_t *>(Dome(), Offsets::OFF_SKY_DOME_COLOURS); }
    static uintptr_t Disc(uint32_t i) {
        return Offsets::VAR_SKY_DISCS + i * Offsets::SKY_DISC_SIZE + Offsets::OFF_SKY_DISC_COLOUR;
    }

  private:
    // The apex, four rings and the rim of a column each, and one more.
    static size_t DomeColourCount() {
        return 5 * static_cast<size_t>(Game::Read<uint32_t>(Dome(), Offsets::OFF_SKY_DOME_COLUMNS)) + 2;
    }

    Record table_{};
    uint8_t slots_[Offsets::OFF_LIGHT_STATE_SKYBOX_WEIGHTS + Offsets::LIGHT_SKYBOX_SLOTS * 4 -
                   Offsets::OFF_LIGHT_STATE_OVERRIDE_SKYBOX] = {};
    float heading_ = 0.0f;
    uint8_t tint_ = 0;
    uint32_t discs_[Offsets::SKY_DISCS_LIT] = {};
    std::vector<uint32_t> domeColours_;
};

// A block of world globals kept to be put back.
class KeepGlobal {
  public:
    KeepGlobal(uintptr_t address, size_t size) : address_(address), bytes_(size) {
        std::memcpy(bytes_.data(), reinterpret_cast<const void *>(address_), size);
    }
    ~KeepGlobal() { std::memcpy(reinterpret_cast<void *>(address_), bytes_.data(), bytes_.size()); }
    KeepGlobal(const KeepGlobal &) = delete;
    KeepGlobal &operator=(const KeepGlobal &) = delete;

  private:
    uintptr_t address_;
    std::vector<uint8_t> bytes_;
};

// What the world's portal walks find from the camera: whether the weather
// byte is set (the sky shows), and the skybox state (a MOSB name or null).
struct PortalSky {
    bool weather = false;
    const char *skybox = nullptr;
};

// The world's walks for the one WMO, from the scene's camera, in the
// device's view: from inside, the interior seed's (0x006B3B20) walk from the
// claimed groups, then its walk from the exterior groups each recorded
// window shows; from outside, where the weather byte is set anyway, the
// exterior seed's (0x006B3DD0) from each exterior group in view, which can
// still name a skybox. The engine's own WalkPortals runs with no group
// callback, so it links nothing into the world's frame; the corner rays are
// the scene camera's, and every global the walks read or write is put back.
// The seeds' last loop (the callback for 0x10000 groups) touches only the
// callback and the liquid state, so it is not run.
constexpr size_t kCornerBytes = 8 * 3 * sizeof(float); // the frustum's eight corners

PortalSky WalkFromCamera(const Backdrop &b, const float (&eye)[3]) {
    using Walk_t = void(__thiscall *)(void *object, uint32_t group, uint32_t from, const float *rect, uint32_t depth,
                                      void *interior);
    using Culled_t = uint32_t(__fastcall *)(const float *box, const float *basis, const float *position);
    using GetMatrix_t = void(__fastcall *)(float *out);
    using GetXform_t = void(__fastcall *)(int unit, float *out);
    using Volume_t = void(__fastcall *)(const float *corners, const float *rect);
    void *object = b.object;
    const bool inside = b.cameraGroupCount != 0;
    PortalSky found;
    found.weather = !inside;
    if (Game::Read<uint8_t>(object, Offsets::OFF_CMAPOBJ_LOADED) == 0)
        return found;
    const KeepGlobal keep[] = {
        {Offsets::VAR_PORTAL_INSIDE_LEG, sizeof(uint32_t)},
        {Offsets::VAR_PORTAL_CALLBACK, sizeof(void *)},
        {Offsets::VAR_PORTAL_CAMERA_POINT, 3 * sizeof(float)},
        {Offsets::VAR_PORTAL_OBJECT_TO_CLIP, sizeof(Matrix)},
        {Offsets::VAR_MAPOBJ_GROUP_FOG_FLAG, sizeof(uint32_t)},
        {Offsets::VAR_LIQUID_GROUP_SEEN, sizeof(uint32_t)},
        {Offsets::VAR_LIQUID_GROUP_PARITY, sizeof(uint32_t)},
        {Offsets::VAR_WEATHER_VISIBLE, sizeof(uint8_t)},
        {Offsets::VAR_SKYBOX_STATE, sizeof(const char *)},
        {Offsets::VAR_CAMERA_CORNER_RAYS, kCornerBytes},
        {Offsets::VAR_PORTAL_WINDOW_COUNT, sizeof(uint32_t)},
        {Offsets::VAR_PORTAL_WINDOWS, Offsets::PORTAL_WINDOW_SLOTS * Offsets::PORTAL_WINDOW_SIZE},
    };
    alignas(16) uint8_t volume[Offsets::FRUSTUM_VOLUME_SIZE] = {};
    BuildViewVolume(volume, eye);
    const auto *corners = reinterpret_cast<const float *>(static_cast<uintptr_t>(Offsets::VAR_CAMERA_CORNER_RAYS));
    std::memcpy(const_cast<float *>(corners), volume + Offsets::OFF_FRUSTUM_VOLUME_CORNERS, kCornerBytes);
    PushObjectSpace(b, eye);
    Matrix device, slot, toClip;
    reinterpret_cast<GetMatrix_t>(Offsets::FUN_GX_GET_VIEW_PROJECTION)(device);
    reinterpret_cast<GetXform_t>(Offsets::FUN_GX_XFORM_GET)(Offsets::GX_XFORM_WORLD, slot);
    Multiply(toClip, slot, device);
    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_PORTAL_OBJECT_TO_CLIP)), toClip,
                sizeof toClip);
    float camera[3];
    TransformPoint(eye, b.inverse, camera);
    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_PORTAL_CAMERA_POINT)), camera,
                sizeof camera);
    Game::Ref<void *>(Offsets::VAR_PORTAL_CALLBACK) = nullptr;
    Game::Ref<uint32_t>(Offsets::VAR_PORTAL_INSIDE_LEG) = inside ? 1u : 0u;
    Game::Ref<uint32_t>(Offsets::VAR_PORTAL_WINDOW_COUNT) = 0;
    Game::Ref<uint8_t>(Offsets::VAR_WEATHER_VISIBLE) = 0;
    Game::Ref<const char *>(Offsets::VAR_SKYBOX_STATE) = nullptr;
    Game::Ref<uint32_t>(Offsets::VAR_MAPOBJ_GROUP_FOG_FLAG) = 1;
    ++Game::Ref<uint32_t>(Offsets::VAR_MAPOBJ_PASS);
    const auto walk = reinterpret_cast<Walk_t>(Offsets::FUN_WALK_PORTALS);
    // A seed's walk from each exterior group that a window ([0, 1]) shows.
    const Matrix &m = b.placement;
    const float basis[9] = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
    const float position[3] = {m[12], m[13], m[14]};
    const auto walkExteriors = [&](const float *window) {
        const float rect[4] = {window[0] * 2.0f - 1.0f, window[1] * 2.0f - 1.0f, window[2] * 2.0f - 1.0f,
                               window[3] * 2.0f - 1.0f};
        reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_PUSH)();
        reinterpret_cast<Volume_t>(Offsets::FUN_BUILD_PORTAL_VOLUME)(corners, window);
        const auto *infos = Game::Read<const uint8_t *>(object, Offsets::OFF_CMAPOBJ_GROUP_INFOS);
        const uint32_t count = Game::Read<uint32_t>(object, Offsets::OFF_CMAPOBJ_GROUP_COUNT);
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *info = infos + i * Offsets::MOGI_RECORD_SIZE;
            const uint32_t flags = Game::Read<uint32_t>(info, 0);
            if ((flags & Offsets::MAPOBJ_GROUP_UNWALKED) != 0 || (flags & Offsets::MOGI_FLAG_EXTERIOR) == 0)
                continue;
            if (reinterpret_cast<Culled_t>(Offsets::FUN_FRUSTUM_BOX_CULLED)(
                    Game::Ptr<float>(info, Offsets::OFF_MOGI_BOUNDS), basis, position) == 0)
                walk(object, i, Offsets::MAPOBJ_PORTAL_FROM_NONE, rect, 0, nullptr);
        }
        reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_POP)();
    };
    if (inside) {
        const float screen[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_PUSH)();
        Game::Ref<uint32_t>(Offsets::VAR_LIQUID_GROUP_SEEN) = 0;
        for (uint32_t i = 0; i < b.cameraGroupCount; ++i) {
            void *group = LoadedGroup(object, b.cameraGroups[i]);
            if (group != nullptr &&
                (Game::Read<uint32_t>(group, Offsets::OFF_CMAPOBJGROUP_FLAGS) & Offsets::MAPOBJ_GROUP_EXTERIOR_MASK) !=
                    0)
                group = nullptr;
            walk(object, b.cameraGroups[i], Offsets::MAPOBJ_PORTAL_FROM_NONE, screen, 0, group);
        }
        reinterpret_cast<Fn_t>(Offsets::FUN_FRUSTUM_POP)();
        Game::Ref<uint32_t>(Offsets::VAR_PORTAL_INSIDE_LEG) = 0;
        const uint32_t windows = Game::Read<uint32_t>(Offsets::VAR_PORTAL_WINDOW_COUNT);
        for (uint32_t w = 0; w < windows; ++w)
            walkExteriors(reinterpret_cast<const float *>(
                static_cast<uintptr_t>(Offsets::VAR_PORTAL_WINDOWS + w * Offsets::PORTAL_WINDOW_SIZE)));
    } else {
        const float whole[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        walkExteriors(whole);
    }
    PopObjectSpace();
    found.weather = found.weather || Game::Read<uint8_t>(Offsets::VAR_WEATHER_VISIBLE) != 0;
    found.skybox = Game::Read<const char *>(Offsets::VAR_SKYBOX_STATE);
    return found;
}

// `mapID` and `cameraLiquid` pick the light; `skybox`, at `skyboxWeight`, is
// a portal walk's.
void DrawSkyPass(uint32_t mapID, uint32_t cameraLiquid, const float (&eye)[3], const float (&forward)[3],
                 const char *skybox, float skyboxWeight) {
    uint8_t *state = LightState();
    if (Game::Read<uint32_t>(state, Offsets::OFF_LIGHT_STATE_SKY_ON) == 0 || SkyObjects::DomeColours() == nullptr)
        return;
    const SkyObjects world;
    // The slots the light's build makes: for a backdrop in a map, the map's
    // light with no screen tint (the player's) and no override; else the
    // world's light and its override.
    Record table;
    const bool own = mapID != kNoMap && BuildMapLight(mapID, cameraLiquid, eye, table);
    const uintptr_t from = own ? Offsets::OFF_LIGHT_STATE_OVERRIDE_SKYBOX : Offsets::OFF_LIGHT_STATE_SKYBOXES;
    std::memset(state + from, 0, Offsets::OFF_LIGHT_STATE_SKYBOX_WEIGHTS + Offsets::LIGHT_SKYBOX_SLOTS * 4 - from);
    if (own) {
        std::memcpy(SkyObjects::Table(), table, sizeof table);
        Game::Ref<uint8_t>(state, Offsets::OFF_LIGHT_STATE_TINT_LEVEL) = 0;
        for (uint32_t i = 0; i < Offsets::SKY_DISCS_LIT; ++i)
            Game::Ref<uint32_t>(SkyObjects::Disc(i)) = Game::Read<uint32_t>(table, Offsets::OFF_LIGHT_RECORD_SUN);
    } else {
        std::memcpy(table, SkyObjects::Table(), sizeof table);
    }
    FillSkyboxSlots(table, state);
    // The walk's skybox (DrawWorldScene 0x00681282): slot 0 at the camera's
    // WMO fog weight, slot 1 cleared.
    if (skybox != nullptr) {
        using SetSlot_t = void(__fastcall *)(int slot, const char *path, float weight);
        const auto setSlot = reinterpret_cast<SetSlot_t>(Offsets::FUN_SET_SKYBOX_SLOT);
        setSlot(0, skybox, skyboxWeight);
        setSlot(1, nullptr, 0.0f);
    }
    Game::Ref<float>(state, Offsets::OFF_LIGHT_STATE_HEADING) = Heading(forward);
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_COLOUR_SKY_DOME)(SkyObjects::Dome());
    const Gx::SavedStates states;
    Matrix identity;
    Identity(identity);
    Gx::PushTransform(Offsets::GX_XFORM_WORLD, identity);
    reinterpret_cast<Fn_t>(Offsets::FUN_DRAW_SKY)();
    Gx::PopTransform(Offsets::GX_XFORM_WORLD);
}

} // namespace

void ClearAndDrawSky(const Backdrop &b, void *frame, const float (&eye)[3]) {
    const PortalSky walked = WalkFromCamera(b, eye);
    const bool dry = b.cameraLiquid == Offsets::LIQUID_DRY;
    const uint32_t colour = !walked.weather ? DrawnFog(b, frame).color : dry ? 0u : ZoneFog(b, frame).color;
    const uint32_t saved = Gx::GetClearColor();
    Gx::SetClearColor(colour);
    Gx::Clear(3);
    Gx::SetClearColor(saved);
    if (walked.weather && dry)
        DrawSkyPass(b.mapID, b.cameraLiquid, eye, CameraView(frame).forward, walked.skybox, b.fog.weight);
}

void ClearAndDrawOpenSky(void *frame, uint32_t mapID, uint32_t cameraLiquid, const float (&eye)[3]) {
    // DrawWorldScene (0x0068123F): the clear colour black when dry, else the
    // fog's (+0x70; the frame's, the zone's under the liquid); the sky only
    // when dry.
    const bool dry = cameraLiquid == Offsets::LIQUID_DRY;
    const uint32_t saved = Gx::GetClearColor();
    Gx::SetClearColor(dry ? 0u : Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR));
    Gx::Clear(3);
    Gx::SetClearColor(saved);
    if (dry)
        DrawSkyPass(mapID, cameraLiquid, eye, CameraView(frame).forward, nullptr, 0.0f);
}

// ---- props: a map's own doodads (the terrain's MDDF) ---------------------------------

struct Props {
    std::vector<std::unique_ptr<Doodad>> doodads;
    bool sounds = false; // SetPlayWorldModelSounds, in the world
    uint32_t queryId = 0;
};

Props *NewProps() { return new Props(); }

void FreeProps(Props *props) {
    if (props == nullptr)
        return;
    for (const auto &d : props->doodads)
        ReleaseDoodad(*d);
    delete props;
}

uint32_t AddProp(Props &props, void *frame, const char *path, const uint8_t *entry) {
    // PlaceDoodadDef (0x00694BC0): the position from the map's corner, the
    // scale word over 1024, the angles in degrees (z a half turn more), each
    // rounded where the client stores it.
    using D = double;
    auto owned = std::make_unique<Doodad>();
    Doodad &d = *owned;
    d.backdrop = nullptr;
    d.sounds = &props.sounds;
    d.frame = frame;
    d.exterior = true; // LoadReferences' flag 4: outdoors, the sun
    float raw[3], angles[3];
    std::memcpy(raw, entry + Offsets::OFF_MDDF_POSITION, sizeof raw);
    std::memcpy(angles, entry + Offsets::OFF_MDDF_ROTATION, sizeof angles);
    const float origin = EngineFloat(Offsets::VAR_MAP_ORIGIN);
    d.position[0] = static_cast<float>(D(-raw[2]) + origin);
    d.position[1] = static_cast<float>(D(origin) + -raw[0]);
    d.position[2] = raw[1];
    const uint16_t scale = Game::Read<uint16_t>(entry, Offsets::OFF_MDDF_SCALE);
    d.scale = static_cast<float>(D(scale) * EngineFloat(Offsets::VAR_ONE_OVER_1024));
    const float k = EngineFloat(Offsets::VAR_DEGREES_TO_RADIANS);
    const float aboutX = static_cast<float>(D(angles[2]) * k);
    const float aboutY = static_cast<float>(D(angles[0]) * k);
    const float aboutZ = static_cast<float>(D(angles[1]) * k + EngineFloat(Offsets::VAR_PI));
    Matrix m;
    Identity(m);
    Translate(m, d.position);
    Rotate(m, aboutZ, {0.0f, 0.0f, 1.0f});
    Rotate(m, aboutY, {0.0f, 1.0f, 0.0f});
    Rotate(m, aboutX, {1.0f, 0.0f, 0.0f});
    Gx::Scale(m, d.scale);
    std::memcpy(d.matrix, m, sizeof d.matrix);
    MakeModel(d, path, m);
    props.doodads.push_back(std::move(owned));
    return static_cast<uint32_t>(props.doodads.size() - 1);
}

void UpdateProps(Props &props, void *frame, bool sounds) {
    props.sounds = sounds && Game::Read<uint8_t>(Offsets::VAR_IN_WORLD) != 0;
    if (!props.sounds)
        SilenceProps(props);
    ShowDoodads(props.doodads, CameraView(frame));
}

void SilenceProps(Props &props) {
    for (const auto &d : props.doodads)
        DropSound(*d);
}

void BeginPropQuery(Props &props) { ++props.queryId; }

bool AddPropFacets(Props &props, uint32_t prop, const float (&box)[6], std::vector<Contact::Facet> &out) {
    // GatherDoodadFacets' rules (0x006ABC40), as for a WMO group's doodads.
    if (prop >= props.doodads.size())
        return true;
    Doodad &d = *props.doodads[prop];
    if (d.stamp == props.queryId || d.model == nullptr)
        return true;
    if (!d.bounded)
        return false;
    if (Touches(d.collision, box))
        AddDoodadFacets(d, box, out);
    d.stamp = props.queryId;
    return true;
}

} // namespace Model::Scene::World
