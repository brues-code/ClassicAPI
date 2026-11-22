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

// `ModelScene` frame type: retail's 3D stage of model actors with an explicit
// camera (FrameAPIModelSceneFrameDocumentation.lua's contract; plan, data
// and probes in docs/notes/model-scene.md). No older client has it, so it is
// built on 1.12's own Model frame, which already is a stage: each Model frame
// has an M2 scene and clock of its own, its paint (SimpleModelDraw 0x0076D240)
// animates and draws that WHOLE scene, and every unattached model in it is
// drawn with its own placement matrix (model-scene.md A1). The stock frame
// just only ever places one model.
//
// So a ModelScene is a CSimpleModel (one SMemAlloc block of the Model size
// plus the fields below, CSimpleModel's constructor, then our copies of its
// two vtables), whose Model half Lua never sees:
//
//     slot                          override
//     +0x00 deleting destructor     destroys the actors, then Model's (which
//                                   frees the camera and the scene)
//     +0x08 GetScriptMethod         our hash, then CSimpleFrame's, skipping
//                                   Model's (0x0076F870 only adds that hash)
//     +0x0C script names            CSimpleFrame's: no OnUpdateModel
//     +0x10 / +0x18 / +0x1C         ModelScene, then Frame's answers: retail's
//                                   IsObjectType("Model") is false
//     +0x28 SetFrameAlpha           Model's, then each actor's model
//     +0x38 OnFrameUpdate           Model's clock, then each actor's update
//     +0x44 EmitLayer               Model's, then our paint on the draw layer
//     +0x98 UpdateModel             places every actor
//     layout +0x04 / +0x08          our destructor / CSimpleFrame's XML loader
//
// The paint mirrors SimpleModelDraw call for call: the device projection,
// view and viewport saved; the camera's projection and view over the frame's
// raw rect (CameraSetDeviceTransforms); the viewport the rect clamped to the
// screen, nothing drawn under 2^-22 of it; a depth clear; the placement (slot
// 0x98, here every actor's); the scene animated from the camera's eye and
// drawn in passes 0 and 1; the three states restored. It has no model gate:
// the collect pass queues only ready models.
//
// The camera is a fixed CCamera made the way the PlayerModel pane makes its
// fallback one (0x00505890) and installed at +0x31C, so the paint's camera leg
// is the engine's. Retail's camera is a position and three axes; the CCamera's
// eye is the position and its target the position plus forward. 1.12's view
// builds its up vector from the camera's yaw and roll properties as (sin yaw
// sin roll, -cos yaw sin roll, cos roll), which can name any unit vector, so
// both are set to name retail's up exactly (the retail probe matched this up
// at every orientation it read). Retail's projection convention is 1.12's own
// (the probe matched 0x005C3CC0 to 0.02%), so retail's field of view and clip
// planes go in unchanged, and so do its defaults: FOV 0.9424778, near 0.2,
// far 100, at the origin looking along +X.
//
// The light is retail's default, set the way PlayerModel's constructor sets
// its own (0x00505680): directional along (0, 1, 0), ambient 178/255, diffuse
// (204, 204, 163)/255, enabled. Each actor's model is lit by it through the
// frame's light callback, as a Model frame's model is, so the light setters
// write the frame's M2Light through the engine's own light setters.
//
// Fog is the Model frame's own (+0x3A4..+0x3B0), always armed: 3.80.2's fog
// block (+0x2E0, set up by 0x1401F29E0) has no enable bit, and its paint
// hands the whole block to the render state every frame (0x14194D860). It
// starts white with near 1e7 and far 1e8, which fog nothing; SetFogNear and
// SetFogFar alone fog to white, and ClearFog (0x140C5AFF0) puts the near and
// far back to 1e7 and 1e8, leaving the colour. SetPaused (0x140C5EED0) stores
// nothing on the scene: it pauses each actor that has a model at the call,
// as that actor's own SetPaused, and the scene clock runs on.
//
// 3.80.2 draws the scene's models and camera at F times their positions,
// F = GetScale x GetEffectiveScale x the DDC height x 5/3 (the paint
// 0x14194D860, the placement 0x141949C00, Project 0x14194CDD0), and leaves
// the clip planes and the fog distances as set. The picture is the same
// (eye and models scale together), but in scene units the clips and the fog
// fall at distance / F. Here the positions stay and those distances are
// divided by F instead (WorldScale). That F is also the retail probe's depth
// factor: k = 0.4607 at scale 1 and 1.8427 at scale 2 on a 21:9 screen, where
// the DDC height 1/sqrt(aspect^2 + 1) is 0.389.
//
// ClassicAPI extensions on that clock: GetSceneTime, and After / NewTimer /
// NewTicker, C_Timer's queue and handles with deadlines in scene seconds,
// fired from the update right after the clock moves. A hidden scene's clock
// stands still, and so do its timers.
//
// XML: `drawLayer`, `fogNear` and `fogFar` (raw, as 3.80.2's loader
// 0x14194CAC0 copies them) and the first <FogColor> (FUN_XML_READ_COLOR),
// after the frame's own loader, and <ViewInsets>. Retail's <Actors> child is
// not read; no retail UI uses it. SetDesaturation gives each actor the scene
// has now its strength (model/Desaturation.cpp draws it).
//
// View insets and translation (retail screenshots, 2026-11-06): the camera's
// picture is built over the frame's rect less the insets, moved by the
// translation (frame units, y up) — a left inset of 100 moved a horse right
// 50 and shrank it to the narrower aspect's scale, a translation of (50, 20)
// moved it right 50 and up 20 at the same size, negative insets reach past
// the frame — but drawing still stops at the frame's edges (a horse moved up
// 120 was cut at the frame's top), and Project3DPointTo2D ignores both. So
// the camera builds its projection over that view rect (CameraSetDevice-
// Transforms takes the aspect from the rect it is given), the projection is
// then scaled and shifted to place the view in the frame, and the viewport
// stays the frame's rect.
//
// SetAllowOverlappedModels (bit 1 of +0x474, default false) decides 3.80.2's
// "Scene Clear Color" pass (0x1410C70E0): with it, depth and stencil are
// cleared over the scene's viewport before its models draw; without it the
// depth already there is kept, unless the UI's target has no depth yet that
// frame. 1.12's UI draws over the world's depth, so the first scene a frame
// paints clears the screen's depth (the UI's fresh target) and the rest keep
// it unless the flag is set. Project3DPointTo2D is under "projection" below.
//
// A scene under a clipping frame or a ScrollFrame is cut to that frame's rect
// as 3.80.2 cuts it (0x1428D60F0 intersects the viewport with the batch's
// clip rect, and keeps the scene's own rect when they do not overlap): the
// clip has narrowed the viewport before the paint runs.
//
// ClassicAPI extensions: GetActorAtCursor, and the scripts OnActorEnter /
// OnActorLeave (self, actor), which run from the update walk when the actor
// under the cursor changes while the scene is the mouse focus. Both use the
// world's own object pick, under "picking" below. GetCursorRay: the world
// frame's pick ray, also under "picking". SetWorldModel /
// SetWorldModelByMapID / ClearWorldModel: a WMO drawn by the world's own map
// object draw after the animate pass, with its doodads as models of the
// scene; Set/GetPlayWorldModelSounds: the doodads' sounds
// (model/SceneWorld.cpp). SetWorldTerrain / ClearWorldTerrain: a map's ADT
// chunks, kept around the scene's camera as the world keeps its own around
// its camera, built and drawn by the world's own chunk functions before the
// WMO (model/SceneTerrain.cpp).

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "texture/Load.h"
#include "api/Documentation.h"
#include "cvar/Factory.h"
#include "frame/ExtraScripts.h"
#include "frame/LayerChange.h"
#include "frame/Layout.h"
#include "frame/ScriptArgs.h"
#include "frame/Subclass.h"
#include "gx/Device.h"
#include "gx/Matrix.h"
#include "lua/MethodArg.h"
#include "model/Camera.h"
#include "model/M2.h"
#include "tick/FrameTick.h"
#include "time/Timer.h"
#include "xml/FrameLoad.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace Model::Scene {

namespace {

using Frame::Layout::OwnerOf;
using Model::Camera::Vec3;
namespace MethodArg = Game::Lua::MethodArg;

// 3.80.2's float epsilon (DAT_14415B534): the light direction's normalize
// threshold and the axis vectors' right-angle test.
constexpr float kAxisEpsilon = 2.3841858e-7f;

// Retail's camera defaults (probe, docs/notes/model-scene.md).
constexpr float kDefaultFieldOfView = 0.9424778f;
constexpr float kDefaultNearClip = 0.2f;
constexpr float kDefaultFarClip = 100.0f;

struct Fields {
    std::vector<Actor *> *actors; // owned, in creation order
    Vec3 position;
    Vec3 forward, right, up;
    float fieldOfView;
    float nearClip;
    float farClip;
    int drawLayer;
    // What the light setters were given, for the getters.
    uint32_t lightType;
    uint32_t lightVisible;
    Vec3 lightDirection;
    Vec3 lightPosition;
    // SetFogNear / SetFogFar as given; the Model frame's fog fields hold them
    // over WorldScale while the scene draws.
    float fogNear;
    float fogFar;
    uint32_t allowOverlappedModels;
    // In the frame's own units: left, right, top, bottom; then x, y (y up).
    float viewInsets[4];
    float viewTranslation[2];
    int32_t groundTerrain; // SetGroundTerrain: a TerrainType id, or -1
    Actor *hovered; // the actor OnActorEnter last ran for, or null
    World::Backdrop *world; // SetWorldModel, owned, or null
    Terrain::Land *terrain; // SetWorldTerrain, owned, or null
    // The camera's liquid (0xF dry), from the last update, and how far below
    // the eye its probe is, from the last paint (World::ProbeDrop).
    uint32_t cameraLiquid;
    float probeDrop;
    Water::Effects *water; // the drift cloud, made when the camera first enters a liquid
    bool playWorldModelSounds; // SetPlayWorldModelSounds
    float timeOfDay; // SetWorldTimeOfDay, in [0, 1), or -1 for the world's
    Actor *listener; // SetSoundListenerActor, or null; the frame owns its actors
    // GetSceneTime: the seconds the M2 scene's clock has advanced since the
    // frame was made, summed from each update's change so it never wraps.
    double clock;
    // SetMoveMarker: the auto-move marker's point, while shown.
    bool moveMarkerShown;
    float moveMarker[3];
};

constexpr size_t kObjectSize = Offsets::MODEL_FRAME_SIZE + sizeof(Fields);

Fields &F(void *frame) { return *Game::Ptr<Fields>(frame, Offsets::MODEL_FRAME_SIZE); }

// 3.80.2's world scale F (0x14194D860): the frame's own scale (Frame:GetScale,
// OFF_FRAME_SCALE) times its effective scale, times the DDC height
// (DAT_144408720, 1.12's VAR_COORD_DDC_HEIGHT_SCALE) and 5/3, at least 0.01 as
// the paint keeps it (+0x2C4). The scene's clip planes and fog distances in
// scene units are what was set over F (see the top of the file).
float WorldScale(void *frame) {
    const float scale = Game::Read<float>(frame, Offsets::OFF_FRAME_SCALE) * Frame::Layout::EffectiveScale(frame) *
                        Game::Read<float>(Offsets::VAR_COORD_DDC_HEIGHT_SCALE) * (5.0f / 3.0f);
    return scale > 0.01f ? scale : 0.01f;
}

// The far clip in scene units, what the scene draws to.
float DrawFarClip(void *frame) { return F(frame).farClip / WorldScale(frame); }

// ---- the camera ------------------------------------------------------------------

void *CameraOf(void *frame) { return Game::Read<void *>(frame, Offsets::OFF_SIMPLEMODEL_CAMERA); }

// Writes the scene's camera into its CCamera.
void ApplyCamera(void *frame) {
    void *camera = CameraOf(frame);
    if (camera == nullptr)
        return;
    const Fields &f = F(frame);
    using namespace Model::Camera;
    SetVector(camera, Offsets::CAMERA_PROP_EYE, f.position);
    SetVector(camera, Offsets::CAMERA_PROP_TARGET,
              {f.position.x + f.forward.x, f.position.y + f.forward.y, f.position.z + f.forward.z});
    // up = (sin yaw sin roll, -cos yaw sin roll, cos roll), roll in [0, pi].
    const float upLength = std::sqrt(f.up.x * f.up.x + f.up.y * f.up.y + f.up.z * f.up.z);
    const float cosRoll = upLength > 0.0f ? f.up.z / upLength : 1.0f;
    const float roll = std::acos(cosRoll < -1.0f ? -1.0f : (cosRoll > 1.0f ? 1.0f : cosRoll));
    const float yaw = (f.up.x != 0.0f || f.up.y != 0.0f) ? std::atan2(f.up.x, -f.up.y) : 0.0f;
    SetScalar(camera, Offsets::CAMERA_PROP_ROLL, roll);
    SetScalar(camera, Offsets::CAMERA_PROP_YAW, yaw);
    SetScalar(camera, Offsets::CAMERA_PROP_FOV, f.fieldOfView);
    const float scale = WorldScale(frame);
    SetScalar(camera, Offsets::CAMERA_PROP_NEAR_CLIP, f.nearClip / scale);
    SetScalar(camera, Offsets::CAMERA_PROP_FAR_CLIP, f.farClip / scale);
}

// Retail's axes from yaw, pitch and roll (Vector3D_CalculateNormalFromYawPitch:
// forward = (cos p cos y, cos p sin y, -sin p), positive pitch looking down;
// the retail probe: right = (0, 1, 0) and up = (0, 0, 1) at rest, and roll
// turns both about forward).
void Orientation(float yaw, float pitch, float roll, Vec3 &forward, Vec3 &right, Vec3 &up) {
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const Vec3 right0{-sy, cy, 0.0f};
    const Vec3 up0{sp * cy, sp * sy, cp};
    forward = {cp * cy, cp * sy, -sp};
    right = {cr * right0.x + sr * up0.x, cr * right0.y + sr * up0.y, cr * right0.z + sr * up0.z};
    up = {cr * up0.x - sr * right0.x, cr * up0.y - sr * right0.y, cr * up0.z - sr * right0.z};
}

void InstallCamera(void *frame) {
    using CameraCreate_t = void *(__cdecl *)();
    using Install_t = void(__thiscall *)(void *frame, void *camera);
    void *camera = reinterpret_cast<CameraCreate_t>(Offsets::FUN_CAMERA_CREATE)();
    if (camera == nullptr)
        return;
    reinterpret_cast<Install_t>(Offsets::FUN_SIMPLEMODEL_INSTALL_CAMERA)(frame, camera);
    Game::ReleaseHandle(camera);
    ApplyCamera(frame);
}

// ---- the light and fog ---------------------------------------------------------------


void *LightOf(void *frame) { return Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT); }
float *AmbientOf(void *frame) { return Game::Ptr<float>(LightOf(frame), Offsets::OFF_M2LIGHT_AMBIENT); }
float *DiffuseOf(void *frame) { return Game::Ptr<float>(LightOf(frame), Offsets::OFF_M2LIGHT_DIFFUSE); }

void SetLightType(void *frame, uint32_t type) {
    F(frame).lightType = type;
    M2::SetLightType(LightOf(frame), static_cast<int>(type));
}

void SetLightVisible(void *frame, bool visible) {
    F(frame).lightVisible = visible ? 1 : 0;
    M2::SetLightEnabled(LightOf(frame), visible);
}

// Stored normalized, as 3.80.2 stores it (0x140C5E660 -> 0x1403117C0): a
// vector whose squared length is above 2.38e-7 is scaled to length 1.
void SetLightDirection(void *frame, Vec3 v) {
    const float lengthSq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (lengthSq > kAxisEpsilon) {
        const float inverse = 1.0f / std::sqrt(lengthSq);
        v = {v.x * inverse, v.y * inverse, v.z * inverse};
    }
    F(frame).lightDirection = v;
    M2::SetLightDirection(LightOf(frame), &v.x);
}

void SetLightPosition(void *frame, const Vec3 &v) {
    F(frame).lightPosition = v;
    M2::SetLightPosition(LightOf(frame), &v.x);
}

// 3.80.2's colour reader byte (0x140A6CC50): clamped to [0, 1], NaN to 0,
// then trunc(c * 255 + 0.5).
uint32_t ColorByte(float c) {
    if (!(c > 0.0f))
        c = 0.0f;
    else if (c > 1.0f)
        c = 1.0f;
    return static_cast<uint32_t>(c * 255.0f + 0.5f);
}

// The colour the reader gives back as floats (0x142A56790): each channel
// quantized to 1/255.
Vec3 QuantizedColor(const Vec3 &c) {
    return {ColorByte(c.x) / 255.0f, ColorByte(c.y) / 255.0f, ColorByte(c.z) / 255.0f};
}

void SetColor(float *color, const Vec3 &v) {
    color[0] = v.x;
    color[1] = v.y;
    color[2] = v.z;
}

// Retail's light, set the way PlayerModel's constructor sets its own
// (0x00505680).
void InitLight(void *frame) {
    SetLightType(frame, 0);
    SetLightDirection(frame, {0.0f, 1.0f, 0.0f});
    SetColor(AmbientOf(frame), {178.0f / 255.0f, 178.0f / 255.0f, 178.0f / 255.0f});
    SetColor(DiffuseOf(frame), {204.0f / 255.0f, 204.0f / 255.0f, 163.0f / 255.0f});
    SetLightVisible(frame, true);
}

// SetFogColor's colour (0x140C5DD00): packed 0xFFRRGGBB through the reader.
void SetFogColor(void *frame, const Vec3 &c) {
    Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR) =
        0xFF000000u | (ColorByte(c.x) << 16) | (ColorByte(c.y) << 8) | ColorByte(c.z);
}

// 3.80.2's fog distances at construction and after ClearFog (0x141393DC0,
// 0x140C5AFF0).
constexpr float kNoFogNear = 1.0e7f;
constexpr float kNoFogFar = 1.0e8f;

// The fog the Model frame's draw applies: always armed, the distances over
// WorldScale. Run before every draw, so a UI scale change takes effect.
void ApplyFog(void *frame) {
    const Fields &f = F(frame);
    const float scale = WorldScale(frame);
    Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS) |= Offsets::SIMPLEMODEL_FOG_ARMED;
    Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR) = f.fogNear / scale;
    Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) = f.fogFar / scale;
}

void InitFog(void *frame) {
    Fields &f = F(frame);
    Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR) = 0xFFFFFFFFu;
    f.fogNear = kNoFogNear;
    f.fogFar = kNoFogFar;
    ApplyFog(frame);
}

// ---- the paint (SimpleModelDraw) -----------------------------------------------

void PlaceActors(void *frame) {
    for (Actor *actor : *F(frame).actors)
        actor->Place();
}

// The walk's entity cull (0x00683340) over the actors, with the view volume
// on the frustum stack: an actor whose model's sphere (its bounds through
// its placement) the volume rejects is hidden, and animated only while its
// centre lies within the squared distance of the camera; off the animate
// list it is neither animated nor drawn. A seated rider goes with its mount
// (the mount animates it).
void CullActors(void *frame, const float (&eye)[3]) {
    const float near = Game::Read<float>(Offsets::VAR_ENTITY_CULLED_ANIMATE_DISTANCE_SQ);
    for (Actor *actor : *F(frame).actors) {
        void *model = actor->model;
        if (model == nullptr || M2::Parent(model) != nullptr || !M2::IsReady(model, 0, 1))
            continue;
        float box[6];
        M2::GetBoundingBox(model, box);
        if (!(box[0] <= box[3] && box[1] <= box[4] && box[2] <= box[5]))
            continue;
        float world[6];
        Gx::TransformBox(M2::BaseMatrix(model), box, world);
        const float centre[3] = {(world[0] + world[3]) * 0.5f, (world[1] + world[4]) * 0.5f,
                                 (world[2] + world[5]) * 0.5f};
        const float radius = std::sqrt(Game::DistanceSq(&world[3], centre));
        if (!World::SphereCulled(centre, radius))
            continue;
        M2::SetVisible(model, false);
        M2::SetAnimated(model, Game::DistanceSq(centre, eye) < near);
    }
}

// SimpleModelDraw's clamp: at or past 1 (and NaN) is 1, at or below 0 is 0.
float ClampUnit(float v) {
    if (!(v < 1.0f))
        return 1.0f;
    return v > 0.0f ? v : 0.0f;
}

// The view: the frame's rect {bottom, left, top, right} less the insets and
// moved by the translation, in layout units (a frame-unit length is
// PixelToInternal(length * effectiveScale)). Returns false when it is the
// frame's rect.
bool ViewRect(void *frame, const float rect[4], float view[4]) {
    const Fields &f = F(frame);
    const float *in = f.viewInsets;
    const float *move = f.viewTranslation;
    if (in[0] == 0.0f && in[1] == 0.0f && in[2] == 0.0f && in[3] == 0.0f && move[0] == 0.0f &&
        move[1] == 0.0f)
        return false;
    auto internal = [frame](float length) { return Frame::Layout::FromLocalUnits(frame, length); };
    view[0] = rect[0] + internal(in[3]) + internal(move[1]);
    view[1] = rect[1] + internal(in[0]) + internal(move[0]);
    view[2] = rect[2] - internal(in[2]) + internal(move[1]);
    view[3] = rect[3] - internal(in[1]) + internal(move[0]);
    return true;
}

// Puts the camera's picture of `view` where `view` lies in `rect`: the
// projection the camera built over `view` (its aspect and centre), then
// scaled and shifted in clip space, because the viewport stays the frame's
// rect. Row-vector matrices: column 0 gives x', column 1 y', column 3 w'.
void FitProjection(const float rect[4], const float view[4]) {
    const float frameW = rect[3] - rect[1], frameH = rect[2] - rect[0];
    if (!(frameW > 0.0f) || !(frameH > 0.0f))
        return;
    const float sx = (view[3] - view[1]) / frameW;
    const float sy = (view[2] - view[0]) / frameH;
    const float tx = ((view[1] + view[3]) - (rect[1] + rect[3])) / frameW;
    const float ty = ((view[0] + view[2]) - (rect[0] + rect[2])) / frameH;
    Gx::Matrix m;
    Gx::GetProjection(m);
    for (int row = 0; row < 4; ++row) {
        float *r = &m[row * 4];
        r[0] = sx * r[0] + tx * r[3];
        r[1] = sy * r[1] + ty * r[3];
    }
    Gx::SetProjection(m);
}

// The device projection and view the paint draws with: the camera framed
// over the view rect, placed in the frame's rect.
void CameraTransforms(void *frame, void *camera, const float (&rect)[4]) {
    float viewRect[4];
    if (ViewRect(frame, rect, viewRect)) {
        Model::Camera::SetDeviceTransforms(camera, viewRect);
        FitProjection(rect, viewRect);
    } else {
        Model::Camera::SetDeviceTransforms(camera, rect);
    }
}

// ---- ground shadows (actor:SetShadowShown) -------------------------------------

// The world's blob shadow (DrawBlobShadow 0x006D7920, Offsets::VAR_SHADOW_LOD):
// Textures\ShadowBlob.blp over the box of the model's sequence (0, 0)
// bounds, clamped to +-5, multiplied by the base scale and turned with the
// base rotation, in white with the model's alpha, multiplied onto what is
// behind it; only while shadowLOD is 1 and the model is ready. The world lays
// it onto its ground with a decal (DrawDecal 0x006D7330): the faces in a box
// from below the feet to above them, the texture placed on them by texgen,
// faded with height by the ramp on stage 1. In a scene with a WMO the decal
// is laid the same way on the WMO's and the terrain's faces (World::DrawDecal).
// A scene without either has no ground, so the turned box itself is drawn,
// flat on the plane of
// the base translation (the model's origin, its feet), culling off as for the
// engine's other single quads (the lightning bolt, 0x007AFCB0), with no
// height fade and no depth function (the quad is its own geometry). A model
// hung on another (a seated rider) casts none: its base transform is relative
// to its parent, and the world's entity list holds the mount alone.
//
// The shadows draw after the WMO and the animate pass and before the models,
// without depth writes: each model then draws over the shadows, its own
// included.

// DrawBlobShadow's part from the model, up to the decal draw.
struct Blob {
    float bounds[6];  // the sequence bounds, clamped and scaled
    float axes[3][3]; // the base rotation: its rows over the scale
    float origin[3];  // the base translation
    float box[6];     // the decal box
    float matrix[16]; // the decal's texture matrix
    uint32_t color;
};

bool MakeBlob(void *model, Blob &blob) {
    float *b = blob.bounds;
    M2::GetSequenceBounds(model, 0, 0, blob.bounds);
    const float epsilon = Game::Read<float>(Offsets::VAR_TWO_POW_MINUS_22);
    if (std::fabs(b[4] - b[1]) < epsilon || std::fabs(b[3] - b[0]) < epsilon)
        return false;
    constexpr float kClamp = static_cast<float>(Offsets::SHADOW_BOUNDS_CLAMP);
    for (float &v : blob.bounds)
        v = v < -kClamp ? -kClamp : (v > kClamp ? kClamp : v);
    float m[16];
    M2::GetBaseTransform(model, m);
    const float scale = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    for (float &v : blob.bounds)
        v *= scale;
    const float height = (b[5] - b[2]) / (static_cast<float>(Offsets::SHADOW_HEIGHT_THIRDS) *
                                          Game::Read<float>(Offsets::VAR_ONE_THIRD));
    // The rotation rows: the scaled axes over the scale, unless it is 1.
    const float inv = std::fabs(scale - 1.0f) < epsilon ? 1.0f : 1.0f / scale;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            blob.axes[i][j] = m[i * 4 + j] * inv;
        blob.origin[i] = m[12 + i];
    }
    // The four corners (x, y, 0) turned, then their bounds moved to the
    // translation; z from below the feet to above them.
    const float corners[4][2] = {{b[3], b[4]}, {b[3], b[1]}, {b[0], b[1]}, {b[0], b[4]}};
    float *box = blob.box;
    for (int c = 0; c < 4; ++c) {
        for (int j = 0; j < 2; ++j) {
            const float v = corners[c][0] * blob.axes[0][j] + corners[c][1] * blob.axes[1][j];
            box[j] = c == 0 || v < box[j] ? v : box[j];
            box[j + 3] = c == 0 || v > box[j + 3] ? v : box[j + 3];
        }
    }
    for (int j = 0; j < 2; ++j) {
        box[j] += blob.origin[j];
        box[j + 3] += blob.origin[j];
    }
    box[2] = blob.origin[2] - Game::Read<float>(Offsets::VAR_SHADOW_BOX_DOWN) * height;
    box[5] = Game::Read<float>(Offsets::VAR_SHADOW_BOX_UP) * height + blob.origin[2];
    // diag(box y, box x, 1) x the rotation transposed x diag(1 / bounds y,
    // 1 / bounds x, 1).
    const float rows[3] = {box[4] - box[1], box[3] - box[0], 1.0f};
    const float columns[3] = {1.0f / std::fabs(b[4] - b[1]), 1.0f / std::fabs(b[3] - b[0]), 1.0f};
    std::memset(blob.matrix, 0, sizeof blob.matrix);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            blob.matrix[i * 4 + j] = rows[i] * blob.axes[j][i] * columns[j];
    blob.matrix[15] = 1.0f;
    float alpha = M2::GetAlpha(model);
    alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    blob.color = (static_cast<uint32_t>(alpha * 255.0f + 0.5f) << 24) | 0x00FFFFFFu;
    return true;
}

// The turned box, flat at the feet.
void DrawQuad(const Blob &blob, const float (&eye)[3]) {
    const float *b = blob.bounds;
    const float x0 = b[0], y0 = b[1], x1 = b[3], y1 = b[4];
    // Strip order; u runs along the model's y, v along its x.
    const float local[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    float pos[4][3];
    float uv[4][2];
    for (int i = 0; i < 4; ++i) {
        const float x = local[i][0], y = local[i][1];
        for (int j = 0; j < 2; ++j)
            pos[i][j] = x * blob.axes[0][j] + y * blob.axes[1][j] + blob.origin[j] - eye[j];
        pos[i][2] = blob.origin[2] - eye[2];
        uv[i][0] = (y - y0) / (y1 - y0);
        uv[i][1] = (x - x0) / (x1 - x0);
    }
    const uint32_t colors[4] = {blob.color, blob.color, blob.color, blob.color};
    static const uint16_t kStrip[4] = {0, 1, 2, 3};
    Gx::PrimStreams(4, pos, 12, nullptr, 0, colors, 4, uv, 8, nullptr, 0);
    Gx::PrimIndexed(Offsets::GX_PRIM_TRIANGLE_STRIP, 4, kStrip);
}

// The ground the paint lays decals on: the WMO and the terrain it draws.
struct Ground {
    World::Backdrop *world = nullptr;
    Terrain::Land *terrain = nullptr;
    bool Any() const { return world != nullptr || terrain != nullptr; }
};

Ground GroundOf(void *frame) {
    if (CameraOf(frame) == nullptr)
        return {};
    return {F(frame).world, F(frame).terrain};
}

// Draws one actor's shadow; the blob states are set.
void DrawShadow(Actor &actor, const Ground &ground, const float (&eye)[3]) {
    void *model = actor.model;
    if (!actor.shadowShown || !actor.shown || model == nullptr || M2::Parent(model) != nullptr ||
        !M2::IsReady(model, 0, 0))
        return;
    Blob blob;
    if (!MakeBlob(model, blob))
        return;
    if (!ground.Any()) {
        DrawQuad(blob, eye);
        return;
    }
    const uint32_t fadeBits = Offsets::DECAL_SHADOW_FADE;
    float fade;
    std::memcpy(&fade, &fadeBits, sizeof fade);
    World::DrawDecal(ground.world, ground.terrain, blob.box, blob.color, blob.matrix, fade,
                     Offsets::SHADOW_DECAL_MASK, eye);
}

// Draws one actor's shadow with the blob states (while shadowLOD is 1).
void DrawShadowWithStates(Actor &actor, const Ground &ground, const float (&eye)[3]) {
    void *texture = Game::Read<void *>(Offsets::VAR_SHADOW_BLOB_TEXTURE);
    if (!actor.shadowShown || Game::Read<int>(Offsets::VAR_SHADOW_LOD) != 1 || texture == nullptr)
        return;
    Gx::PushStates();
    Gx::SetState(Offsets::GXRS_BLEND, Offsets::GXRS_BLEND_MODULATE);
    Gx::SetState(Offsets::GXRS_LIGHTING, 0);
    Gx::SetState(Offsets::GXRS_FOG, 0);
    Gx::SetState(Offsets::GXRS_DEPTH_WRITE, 0);
    Gx::SetState(Offsets::GXRS_TEXTURE0, Texture::DeviceTexture(texture));
    Gx::SetStateWord(Offsets::GXRS_FOG_COLOR, 0xFFFFFFFFu);
    Gx::SetState(Offsets::GXRS_COMBINER0, Offsets::GXRS_COMBINE_BLEND_DIFFUSE_ALPHA);
    if (!ground.Any())
        Gx::SetState(Offsets::GXRS_CULL, 0);
    else if (Game::Read<float>(Offsets::VAR_SHADOW_BIAS) == 0.0f)
        Gx::SetState(Offsets::GXRS_DEPTH_FUNC, Offsets::GXRS_DEPTH_FUNC_DECAL);
    DrawShadow(actor, ground, eye);
    Gx::PopStates();
}

// ---- selection circles and the move marker (SetSelectionCircle, SetMoveMarker) --
//
// The world draws the target's selection circle (0x00608E00) and the
// auto-move marker (0x00611400) with one decal recipe: a texture matrix
// turned about z by minus the heading from the camera to the point
// (0x00615070, given the scene's eye for the world camera's), then inside a
// state push, in the scene's fog for the world's (World::CommitDecalFog),
// the circle's state (FUN_SELECTION_CIRCLE_STATE: blend 3, which
// adds by source alpha, UnitSelectTexture.blp and its ramp) and the decal at
// fade 0.5, flags 0x200122. In a scene with a WMO or terrain the decal lands
// on their faces (World::DrawDecal), as the shadows do; without either, on
// the flat ground at the feet (World::DrawDecalOnPlane).
//
// The circle: the box about the feet, half size r across and 2r up and
// down, r = the radius the unit takes when its model loads (0x0060AEE0,
// Offsets::SELECTION_RADIUS_FLAT) times the model's scale; only while the
// ObjectSelectionCircle cvar is on, as the world's (0x00614ADA). Drawn per
// actor before its shadow, as the world's per-entity step draws it before
// the blob. The marker: the auto-move target's pulsing box (Offsets::
// VAR_AUTOMOVE_MARKER_*), after the actors', on the scene clock where the
// world's runs on the OS clock: the scene clock is the scene's world time.

bool SelectionDecalReady() {
    return Game::Read<void *>(Offsets::VAR_SELECTION_CIRCLE_TEXTURE) != nullptr &&
           Game::Read<void *>(Offsets::VAR_SELECTION_CIRCLE_RAMP) != nullptr;
}

void DrawSelectionDecal(void *frame, const Ground &ground, const float (&box)[6], uint32_t argb,
                        const float (&point)[3], float groundZ, const float (&eye)[3]) {
    using Heading_t = float(__fastcall *)(const float *from, const float *to);
    using CircleState_t = void(__thiscall *)(void *obj);
    float m[16];
    Gx::Identity(m);
    const float heading = reinterpret_cast<Heading_t>(Offsets::FUN_HEADING_BETWEEN_POINTS)(eye, point);
    Gx::Rotate(m, -heading, {0.0f, 0.0f, 1.0f});
    const uint32_t fadeBits = Offsets::DECAL_SELECTION_FADE;
    float fade;
    std::memcpy(&fade, &fadeBits, sizeof fade);
    Gx::PushStates();
    World::CommitDecalFog(ground.world, frame, point, eye);
    reinterpret_cast<CircleState_t>(Offsets::FUN_SELECTION_CIRCLE_STATE)(nullptr);
    if (ground.Any())
        World::DrawDecal(ground.world, ground.terrain, box, argb, m, fade, Offsets::DECAL_SELECTION_FLAGS, eye);
    else
        World::DrawDecalOnPlane(box, argb, m, fade, groundZ, eye);
    Gx::PopStates();
}

// 0x0060AEE0's radius for the model.
float SelectionRadius(void *model) {
    float bounds[6];
    if (M2::HasAnimation(model, 0))
        M2::GetSequenceBounds(model, 0, 0, bounds);
    else
        M2::GetBoundingBox(model, bounds);
    if (bounds[3] == bounds[0] && bounds[4] == bounds[1]) {
        const uint32_t flatBits = Offsets::SELECTION_RADIUS_FLAT;
        float flat;
        std::memcpy(&flat, &flatBits, sizeof flat);
        return flat;
    }
    const float dx = bounds[3] - bounds[0];
    const float dy = bounds[4] - bounds[1];
    return std::sqrt(std::sqrt(dx * dx + dy * dy) * Game::Read<float>(Offsets::VAR_HALF));
}

void DrawSelectionCircle(void *frame, Actor &actor, const Ground &ground, const float (&eye)[3]) {
    void *model = actor.model;
    if (!actor.selectionCircleShown || !actor.shown || model == nullptr || M2::Parent(model) != nullptr ||
        !M2::IsReady(model, 0, 0))
        return;
    float m[16];
    M2::GetBaseTransform(model, m);
    const float scale = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float r = SelectionRadius(model) * scale;
    const float feet[3] = {m[12], m[13], m[14]};
    const float box[6] = {feet[0] - r, feet[1] - r, feet[2] - 2.0f * r,
                          feet[0] + r, feet[1] + r, feet[2] + 2.0f * r};
    DrawSelectionDecal(frame, ground, box, actor.selectionCircle, feet, feet[2], eye);
}

void DrawMoveMarker(void *frame, const Ground &ground, const float (&eye)[3]) {
    const Fields &f = F(frame);
    if (!f.moveMarkerShown)
        return;
    const uint32_t period = Game::Read<uint32_t>(Offsets::VAR_AUTOMOVE_MARKER_PERIOD_MS);
    const uint32_t t = ClockMs(frame) % period;
    const float low = Game::Read<float>(Offsets::VAR_AUTOMOVE_MARKER_MIN);
    const float high = Game::Read<float>(Offsets::VAR_AUTOMOVE_MARKER_MAX);
    const float wave = (std::sin(static_cast<float>(t) * Game::Read<float>(Offsets::VAR_HEADING_TWO_PI) /
                                 static_cast<float>(period)) +
                        Game::Read<float>(Offsets::VAR_ONE)) *
                       Game::Read<float>(Offsets::VAR_HALF);
    const float s = wave * (high - low) + low;
    const float(&p)[3] = f.moveMarker;
    const float box[6] = {p[0] - s, p[1] - s, p[2] - s, p[0] + s, p[1] + s, p[2] + s};
    DrawSelectionDecal(frame, ground, box, Offsets::AUTOMOVE_MARKER_COLOR, p, p[2], eye);
}

// ---- the ground-target reticle (SetGroundReticle) ----------------------------------
//
// The world shows where a ground-targeted spell will land with one reticle
// at the pointer (0x004837B0); here any actor can show one at its position,
// so a scene can mark several spots at once. The world's recipe: the box
// about the point, half size the radius across (1.3888889 for a radius of
// 0) and 2 yards up and down; inside a state push lighting and depth write
// off, blend 2 and the in-range or out-of-range texture; then the decal in
// white, with no texture matrix (it does not turn with the camera, as the
// selection circle does) and fade 0.5, onto terrain and buildings. It is
// drawn after the opaque models and before the water and the translucent
// models (0x004836B0), in the world's fog, so here in the scene's
// (World::CommitDecalFog). Without a WMO or terrain it lies on the flat
// ground at the actor's height. The textures exist once the world frame is built, so at
// the login and character screens no reticle shows.

void DrawGroundReticle(void *frame, const Actor &actor, const Ground &ground, const float (&eye)[3]) {
    if (!actor.groundReticleShown || !actor.shown)
        return;
    void *texture = Game::Read<void *>(actor.groundReticleValid ? Offsets::VAR_RETICLE_TEXTURE_ACCEPTABLE
                                                                : Offsets::VAR_RETICLE_TEXTURE_UNACCEPTABLE);
    if (texture == nullptr)
        return;
    const float r = GroundReticleRadius(actor);
    const Actor::Point origin = actor.Origin();
    const float(&p)[3] = origin.p;
    const float two = Game::Read<float>(Offsets::VAR_TWO);
    const float box[6] = {p[0] - r, p[1] - r, p[2] - two, p[0] + r, p[1] + r, p[2] + two};
    float m[16];
    Gx::Identity(m);
    const uint32_t fadeBits = Offsets::RETICLE_FADE;
    float fade;
    std::memcpy(&fade, &fadeBits, sizeof fade);
    Gx::PushStates();
    World::CommitDecalFog(ground.world, frame, p, eye);
    Gx::SetState(Offsets::GXRS_LIGHTING, 0);
    Gx::SetState(Offsets::GXRS_DEPTH_WRITE, 0);
    Gx::SetState(Offsets::GXRS_BLEND, Offsets::RETICLE_BLEND);
    Gx::SetState(Offsets::GXRS_TEXTURE0, Texture::DeviceTexture(texture));
    if (ground.Any())
        World::DrawDecal(ground.world, ground.terrain, box, Offsets::RETICLE_COLOR, m, fade,
                         Offsets::DECAL_SELECTION_FLAGS, eye);
    else
        World::DrawDecalOnPlane(box, Offsets::RETICLE_COLOR, m, fade, p[2], eye);
    Gx::PopStates();
}

void DrawGroundReticles(void *frame, const float (&eye)[3]) {
    const Ground ground = GroundOf(frame);
    for (const Actor *actor : *F(frame).actors)
        DrawGroundReticle(frame, *actor, ground, eye);
}

// What the world lays on the ground under its entities, for the actors.
void DrawGroundDecals(void *frame, const float (&eye)[3]) {
    const Ground ground = GroundOf(frame);
    const bool decals = SelectionDecalReady();
    void *cvar = Game::Read<void *>(Offsets::VAR_CVAR_OBJECT_SELECTION_CIRCLE);
    const bool circles = decals && cvar != nullptr && CVar::Factory::GetInt(cvar) != 0;
    for (Actor *actor : *F(frame).actors) {
        if (circles)
            DrawSelectionCircle(frame, *actor, ground, eye);
        DrawShadowWithStates(*actor, ground, eye);
    }
    if (decals)
        DrawMoveMarker(frame, ground, eye);
}

// Whether the UI's depth is still the world's this frame: set at the head of
// each UI frame, cleared by the first scene that paints (see the top of the
// file).
bool g_worldDepth = true;

void ResetUiDepth() { g_worldDepth = true; }

const Tick::FrameTick::AutoSubscribeBefore _uiDepth{&ResetUiDepth};

void __fastcall Paint(void *frame) {
    using SetupOrtho_t = void(__fastcall *)(const float *rect, const float *point, int unused);

    // 3.80.2's paint draws nothing unless the near clip is before the far.
    if (!(F(frame).nearClip < F(frame).farClip))
        return;
    // {bottom, left, top, right}
    float rect[4] = {};
    if (!Frame::Layout::GetRect(frame, rect))
        return;
    ApplyCamera(frame);
    ApplyFog(frame);
    const Gx::SavedTransforms saved;
    const Gx::Viewport &vp = saved.SavedViewport();
    float eye[3] = {0.0f, 0.0f, 0.0f};
    if (void *camera = CameraOf(frame)) {
        const Vec3 e = Model::Camera::GetVector(camera, Offsets::CAMERA_PROP_EYE);
        eye[0] = e.x;
        eye[1] = e.y;
        eye[2] = e.z;
        // The shakes move the eye, not the view (the world's driver adds
        // them to the eye, 0x0050EDCC); everything draws from the moved eye.
        const Fields &f = F(frame);
        float shake[3];
        Shakes::Offset(frame, eye, std::atan2(f.forward.y, f.forward.x), shake);
        for (int i = 0; i < 3; ++i)
            eye[i] += shake[i];
        CameraTransforms(frame, camera, rect);
    } else {
        const float point[2] = {rect[1], rect[0]};
        reinterpret_cast<SetupOrtho_t>(Offsets::FUN_SETUP_ORTHO_TRANSFORMS)(rect, point, 0);
    }
    Frame::Layout::DdcToNdc(&rect[1], &rect[0], rect[1], rect[0]);
    Frame::Layout::DdcToNdc(&rect[3], &rect[2], rect[3], rect[2]);
    for (float &edge : rect)
        edge = ClampUnit(edge);
    // The viewport is the rect cut to the clip a clipping frame or a
    // ScrollFrame has narrowed the viewport to (0x1428D60F0's intersection),
    // and the camera's picture of the whole rect is cropped to it. 3.80.2
    // keeps the whole rect when they do not overlap; here that draws nothing,
    // as 1.12's ScrollFrame relies on the clip to hide what it scrolled away.
    const float port[4] = {std::max(rect[0], vp.minY), std::max(rect[1], vp.minX), std::min(rect[2], vp.maxY),
                           std::min(rect[3], vp.maxX)};
    if (!(port[0] < port[2]) || !(port[1] < port[3]))
        return;
    const float epsilon = Game::Read<float>(Offsets::VAR_TWO_POW_MINUS_22); // 2^-22
    if (!(std::fabs(port[1] - port[3]) < epsilon) && !(std::fabs(port[0] - port[2]) < epsilon)) {
        if (g_worldDepth) {
            Gx::SetViewport({0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f});
            Gx::Clear(2);
            g_worldDepth = false;
        }
        Gx::SetViewport({port[1], port[3], port[0], port[2], 0.0f, 1.0f});
        if (port[0] != rect[0] || port[1] != rect[1] || port[2] != rect[2] || port[3] != rect[3])
            FitProjection(port, rect);
        World::Backdrop *world = CameraOf(frame) != nullptr ? F(frame).world : nullptr;
        Terrain::Land *terrain = CameraOf(frame) != nullptr ? F(frame).terrain : nullptr;
        World::ViewVolume volume;
        World::BuildViewVolume(volume, eye);
        F(frame).probeDrop = World::ProbeDrop(volume, eye);
        // The world's frame under a liquid: its fog the map's for that liquid.
        const uint32_t liquid = CameraLiquid(frame);
        const uint32_t map = terrain != nullptr ? Terrain::MapOf(*terrain)
                             : world != nullptr ? World::MapOf(*world)
                                                : World::kNoMap;
        // A scene with its own time of day is lit at it, before the liquid's
        // fog takes the map's light.
        const float farClip = DrawFarClip(frame);
        const World::Daylight daylight(frame, map, liquid, eye, farClip, F(frame).timeOfDay);
        const World::LiquidFog liquidFog(frame, map, liquid, eye, farClip);
        const bool wet = liquid != Offsets::LIQUID_DRY;
        if (world != nullptr)
            World::ClearAndDrawSky(*world, frame, eye, volume);
        else if (terrain != nullptr)
            World::ClearAndDrawOpenSky(frame, map, liquid, eye);
        else if (F(frame).allowOverlappedModels != 0)
            Gx::Clear(2);
        PlaceActors(frame);
        // The world's walk elects what it animates and draws before the
        // animate pass (0x00682FA0, then 0x00707680).
        World::PushFrustum(volume);
        CullActors(frame, eye);
        if (world != nullptr)
            World::CullDoodads(*world, eye);
        if (terrain != nullptr)
            Terrain::CullProps(*terrain, eye);
        World::PopFrustum();
        void *scene = Model::Pane::Scene(frame);
        M2::AnimateScene(scene, eye);
        // The world's frame draws its chunks before its map objects, and the
        // terrain's magma after them (DrawWorldScene 0x006812B1..0x006812BB).
        if (terrain != nullptr)
            Terrain::Draw(*terrain, frame, eye, volume, farClip);
        if (world != nullptr)
            World::Draw(*world, frame, eye, volume);
        if (terrain != nullptr)
            Terrain::DrawMagma(*terrain, frame, eye, volume, farClip);
        DrawGroundDecals(frame, eye);
        M2::DrawScene(scene, 0);
        DrawGroundReticles(frame, eye);
        // The world's frame draws its liquids before the translucent models
        // over the water and, with the camera in liquid, after them
        // (0x004836D6).
        if (wet)
            M2::DrawScene(scene, 1);
        // RenderLiquid (0x006816D0): the terrain's ocean and river, then the
        // map objects' liquids.
        if (terrain != nullptr)
            Terrain::DrawWater(*terrain, frame, eye, volume, farClip, liquid);
        if (world != nullptr)
            World::DrawLiquids(*world, frame, eye);
        Water::DrawRipples(F(frame).water, eye);
        if (!wet)
            M2::DrawScene(scene, 1);
        Chains::Render(frame, eye);
        // The world stage's particulates follow its models (0x006701E0).
        Water::DrawDrift(F(frame).water, liquid);
    }
}

// ---- projection (Project3DPointTo2D) ---------------------------------------------

// A scene point through the matrices the paint draws with: the device's
// projection and view saved, CameraSetDeviceTransforms run over the frame's
// rect, both read back, both restored. The view looks from the origin (the
// eye is not in it, benilla camera.md), so the point goes in relative to the
// eye, and the matrices are row-vector (0x005C3CC0 puts 1 at m11: w' is the
// view depth). A |w'| under 0.001 projects nothing, the engine's own rule
// (0x005C4F30).
//
// Retail probe (12.1.5, 400x200 frame, effective scale 0.7111, camera at
// (-10, 0, 0) looking along +X): (0,0,0) -> (142.22, 71.11), the centre in
// frame units x effective scale from the bottom-left; (0,2,1) -> 66.49 left
// and 33.25 up of it, this matrix to every digit; a point behind the camera
// still projects. Depth was (far - k * viewDepth) / (far - near): planar
// (the off-axis point had the on-axis depth), the same k across far 100/200,
// near 0.2/1, width and field of view, k = 0.4607 at scale 1 and 1.8427 at
// SetScale(2). That k is WorldScale: 3.80.2 (0x14194CDD0) multiplies the
// point and the camera by F before it projects, with the clips unscaled. So
// depth here is (far/F - viewDepth) / (far/F - near/F), the same value, from
// the camera ApplyCamera sets. 3.80.2 divides by w whatever it is; |w| under
// 0.001 projects nothing here, the 1.12 engine's own rule (0x005C4F30).
//
// The view insets and translation do not move the projection: retail gave
// (142.22, 71.11) with none, with SetViewInsets(100, 0, 0, 0) and with
// SetViewTranslation(50, 0) (12.1.5, same frame). So this frames the
// camera over the whole rect, not the paint's view rect.
bool Project(void *frame, const Vec3 &point, double out[3]) {
    void *camera = CameraOf(frame);
    float rect[4] = {}; // {bottom, left, top, right}
    if (camera == nullptr || !Frame::Layout::GetRect(frame, rect))
        return false;
    ApplyCamera(frame);
    Gx::Matrix projection, view;
    {
        const Gx::SavedTransforms saved;
        Model::Camera::SetDeviceTransforms(camera, rect);
        Gx::GetProjection(projection);
        Gx::GetView(view);
    }

    const Vec3 eye = Model::Camera::GetVector(camera, Offsets::CAMERA_PROP_EYE);
    const double p[4] = {point.x - eye.x, point.y - eye.y, point.z - eye.z, 1.0};
    double v[4], c[4];
    for (int col = 0; col < 4; ++col) {
        v[col] = 0.0;
        for (int row = 0; row < 4; ++row)
            v[col] += p[row] * view[row * 4 + col];
    }
    for (int col = 0; col < 4; ++col) {
        c[col] = 0.0;
        for (int row = 0; row < 4; ++row)
            c[col] += v[row] * projection[row * 4 + col];
    }
    if (!(std::fabs(c[3]) >= 0.001))
        return false;
    const double width = Game::InternalToPixel(rect[3] - rect[1]);
    const double height = Game::InternalToPixel(rect[2] - rect[0]);
    out[0] = (c[0] / c[3] + 1.0) * 0.5 * width;
    out[1] = (c[1] / c[3] + 1.0) * 0.5 * height;
    const Fields &f = F(frame);
    const double scale = WorldScale(frame);
    const double nearClip = f.nearClip / scale, farClip = f.farClip / scale;
    out[2] = farClip != nearClip ? (farClip - c[3]) / (farClip - nearClip) : 0.0;
    return true;
}

// ---- picking (GetActorAtCursor) ---------------------------------------------------

// The world's object pick (0x00480A50) on the scene's own M2 scene: ray hits
// on, each candidate's model and its children on the hit list
// (SetHitTestMode, as 0x00480D90 does it), then the engine's ray, which
// skins each model on the CPU and returns the value of the nearest triangle
// (models/scene.md: Ray hits). So an actor is picked by what it draws, not
// by its bounding box. The ray is in view space and the models are where the
// scene's latest paint put them (RayHit skips a model that paint did not
// animate), so the pick is what the frame shows.
//
// The ray is the cursor through the paint's projection (CameraTransforms):
// the camera's rect aspect (CCamera::SetDeviceTransforms 0x007AC640 reads
// nothing else of the rect) in the viewport the paint sets, the frame's rect
// clamped to the screen. It runs from the near clip to the far clip, by view
// depth (w', which FitProjection does not change).

// The view-space point at clip (x, y) and view depth `depth`: solves
// x' = x w', y' = y w', w' = depth for the row-vector projection `m`.
bool Unproject(const Gx::Matrix &m, float x, float y, float depth, float (&out)[3]) {
    double a[3][3], b[3];
    for (int j = 0; j < 3; ++j) {
        a[0][j] = m[j * 4 + 0] - x * m[j * 4 + 3];
        a[1][j] = m[j * 4 + 1] - y * m[j * 4 + 3];
        a[2][j] = m[j * 4 + 3];
    }
    b[0] = x * m[15] - m[12];
    b[1] = y * m[15] - m[13];
    b[2] = depth - m[15];
    auto det = [](const double (&r)[3][3]) {
        return r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
               r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
               r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    };
    const double d = det(a);
    if (!(std::fabs(d) > 1e-12))
        return false;
    for (int j = 0; j < 3; ++j) {
        double c[3][3];
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k)
                c[r][k] = k == j ? b[r] : a[r][k];
        out[j] = static_cast<float>(det(c) / d);
    }
    return true;
}

// The cursor's fractions across and up the paint's viewport, the frame's
// rect {bottom, left, top, right} in NDC clamped to the screen, each 0 to 1;
// false when the cursor is outside it or the paint draws nothing.
bool CursorFraction(const float (&rect)[4], float &x, float &y) {
    const uint8_t *ctx = Game::Read<const uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    if (ctx == nullptr)
        return false;
    const float *mouse = Game::Ptr<float>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    float vp[4] = {rect[0], rect[1], rect[2], rect[3]};
    Frame::Layout::DdcToNdc(&vp[1], &vp[0], vp[1], vp[0]);
    Frame::Layout::DdcToNdc(&vp[3], &vp[2], vp[3], vp[2]);
    for (float &edge : vp)
        edge = ClampUnit(edge);
    const float epsilon = Game::Read<float>(Offsets::VAR_TWO_POW_MINUS_22);
    if (std::fabs(vp[1] - vp[3]) < epsilon || std::fabs(vp[0] - vp[2]) < epsilon)
        return false;
    if (!(mouse[0] >= vp[1] && mouse[0] <= vp[3] && mouse[1] >= vp[0] && mouse[1] <= vp[2]))
        return false;
    x = (mouse[0] - vp[1]) / (vp[3] - vp[1]);
    y = (mouse[1] - vp[0]) / (vp[2] - vp[0]);
    return true;
}

// The model and its children (held items, kit effects) on the hit list as
// `actor`. A child that is another actor's model (a seated rider) is that
// actor's to add.
void AddHitTest(void *model, Actor *actor, const std::vector<Actor *> &actors) {
    M2::SetHitTest(model, reinterpret_cast<uintptr_t>(actor));
    for (void *child = M2::FirstChild(model); child != nullptr; child = M2::NextSibling(child)) {
        bool otherActor = false;
        for (const Actor *a : actors)
            otherActor = otherActor || a->model == child;
        if (!otherActor)
            AddHitTest(child, actor, actors);
    }
}

// The shown actor whose model is nearest under the cursor, or null.
Actor *ActorAtCursor(void *frame) {
    void *camera = CameraOf(frame);
    float rect[4] = {}; // {bottom, left, top, right}
    if (camera == nullptr || !Model::Pane::IsVisible(frame) || !Frame::Layout::GetRect(frame, rect))
        return nullptr;
    float x = 0.0f, y = 0.0f;
    if (!CursorFraction(rect, x, y))
        return nullptr;
    x = 2.0f * x - 1.0f; // into clip space
    y = 2.0f * y - 1.0f;
    Gx::Matrix projection;
    {
        const Gx::SavedTransforms saved;
        CameraTransforms(frame, camera, rect);
        Gx::GetProjection(projection);
    }
    const Fields &f = F(frame);
    float p0[3], p1[3];
    if (!Unproject(projection, x, y, f.nearClip, p0) || !Unproject(projection, x, y, f.farClip, p1))
        return nullptr;
    void *scene = Model::Pane::Scene(frame);
    M2::EnableRayHits(scene);
    for (Actor *actor : *f.actors)
        if (actor->shown && actor->model != nullptr)
            AddHitTest(actor->model, actor, *f.actors);
    return reinterpret_cast<Actor *>(M2::RayHit(scene, p0, p1));
}

// The world frame's pick ray (0x004813B0, from PickAtCursor 0x00481190):
// the cursor's fractions across and up the viewport, through the engine's
// UnprojectScreenPoint (0x007AD2C0) under the paint's device transforms,
// then the eye added to the near and far plane points, as the camera's view
// leaves it out. Scene coordinates; false when the cursor is not over the
// shown picture.
bool CursorRay(void *frame, float (&nearPoint)[3], float (&farPoint)[3]) {
    void *camera = CameraOf(frame);
    float rect[4] = {}; // {bottom, left, top, right}
    if (camera == nullptr || !Model::Pane::IsVisible(frame) || !Frame::Layout::GetRect(frame, rect))
        return false;
    float a = 0.0f, b = 0.0f;
    if (!CursorFraction(rect, a, b))
        return false;
    {
        const Gx::SavedTransforms saved;
        CameraTransforms(frame, camera, rect);
        Gx::UnprojectScreenPoint(nearPoint, farPoint, a, b);
    }
    const Vec3 eye = Model::Camera::GetVector(camera, Offsets::CAMERA_PROP_EYE);
    const float offset[3] = {eye.x, eye.y, eye.z};
    for (int i = 0; i < 3; ++i) {
        nearPoint[i] += offset[i];
        farPoint[i] += offset[i];
    }
    return true;
}

// ---- the overrides -----------------------------------------------------------------

using Hide_t = int(__fastcall *)(void *self, void *edx);
using Callback_t = void(__fastcall *)(void *arg);
using AddCallback_t = void(__fastcall *)(void *batch, void *edx, Callback_t fn, void *arg);

// The actors destroyed, before Model's deleting destructor (which frees the
// camera and the scene).
void Destroy(void *frame) {
    Fields &f = F(frame);
    Chains::Destroy(frame);
    Shakes::Destroy(frame);
    for (Actor *actor : *f.actors)
        delete actor;
    delete f.actors;
    f.actors = nullptr;
    World::Free(f.world);
    f.world = nullptr;
    Terrain::Free(f.terrain);
    f.terrain = nullptr;
    Water::Free(f.water);
    f.water = nullptr;
    Time::Timer::CancelOwned(frame);
}

Frame::Subclass::Class<Offsets::VTBL_SIMPLEMODEL_SLOTS, Offsets::VTBL_SIMPLEMODEL_LAYOUT_SLOTS>
    g_class{"ModelScene",
            Offsets::VTBL_SIMPLEMODEL,
            Offsets::VTBL_SIMPLEMODEL_LAYOUT,
            Offsets::FUN_SIMPLEMODEL_CONSTRUCTOR,
            kObjectSize,
            &Destroy};

using Frame::Subclass::EmitLayer_t;
using Frame::Subclass::LoadXML_t;
using Frame::Subclass::OnFrameUpdate_t;
using Frame::Subclass::SetFrameAlpha_t;

// OnActorEnter / OnActorLeave (self, actor): the actor under the cursor
// changed while the scene is the mouse focus, from the update walk.
const Frame::ExtraScripts::Script g_onActorEnter{"onactorenter", g_class.TypeIdVar()};
const Frame::ExtraScripts::Script g_onActorLeave{"onactorleave", g_class.TypeIdVar()};

bool HasScript(const Frame::ExtraScripts::Script &script, void *frame) {
    const uint32_t *slot = script.Slot(frame);
    return slot != nullptr && slot[0] != 0;
}

void RunActorScript(const Frame::ExtraScripts::Script &script, void *frame, Actor *actor) {
    Frame::ScriptArgs::RunSlot(script.Slot(frame), frame, [actor](void *L) {
        Animation::Push(L, actor);
        return 1;
    });
}

bool IsMouseFocus(void *frame) {
    const uint8_t *ctx = Game::Read<const uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    return ctx != nullptr && Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS) == frame;
}

// Picks only while a script would hear of it.
void UpdateHover(void *frame) {
    Fields &f = F(frame);
    Actor *now = nullptr;
    if ((HasScript(g_onActorEnter, frame) || HasScript(g_onActorLeave, frame)) && IsMouseFocus(frame))
        now = ActorAtCursor(frame);
    Actor *was = f.hovered;
    if (now == was)
        return;
    f.hovered = now;
    if (was != nullptr)
        RunActorScript(g_onActorLeave, frame, was);
    // OnActorLeave may have moved on to another actor.
    if (now != nullptr && f.hovered == now)
        RunActorScript(g_onActorEnter, frame, now);
}

void __fastcall SetFrameAlpha(void *self, void *, uint8_t alpha) {
    g_class.Base<SetFrameAlpha_t>(Offsets::VTBL_FRAME_SET_FRAME_ALPHA)(self, nullptr, alpha);
    for (Actor *actor : *F(self).actors)
        actor->ApplyAlpha();
}

void __fastcall OnFrameUpdate(void *self, void *, float elapsedSec) {
    for (Actor *actor : *F(self).actors)
        actor->Link();
    Terrain::Land *terrain = F(self).terrain;
    // The world's map update runs first in its frame (0x00698390): the
    // chunks around the camera, then the map objects they place.
    if (terrain != nullptr && Terrain::Stream(*terrain, self, CameraView(self).eye, DrawFarClip(self)))
        World::SetTileObjects(F(self).world, Terrain::MapObjects(*terrain), Terrain::MapOf(*terrain));
    World::Backdrop *world = F(self).world;
    if (world != nullptr)
        World::Update(*world, terrain, self, F(self).playWorldModelSounds, elapsedSec);
    if (terrain != nullptr)
        Terrain::Update(*terrain, self, F(self).playWorldModelSounds);
    const uint32_t lastLiquid = F(self).cameraLiquid;
    F(self).cameraLiquid = world != nullptr ? World::CameraLiquid(*world) : OutdoorCameraLiquid(self);
    Water::Update(F(self).water, lastLiquid, F(self).cameraLiquid, CameraView(self).eye, elapsedSec,
                  static_cast<double>(ClockMs(self)) / 1000.0);
    // A seated rider or an attached actor takes its parent's light.
    if (world != nullptr || terrain != nullptr)
        for (Actor *actor : *F(self).actors)
            if (actor->model != nullptr && M2::Parent(actor->model) == nullptr && M2::IsLoaded(actor->model, 0, 0))
                World::Settle(world, terrain, *actor->ambience, self, actor->model, elapsedSec);
    const uint32_t before = ClockMs(self);
    g_class.Base<OnFrameUpdate_t>(Offsets::VTBL_FRAME_ON_FRAME_UPDATE)(self, nullptr, elapsedSec);
    // The scene's timers, movement and casts run on its clock, once it has
    // moved this frame.
    const uint32_t advancedMs = ClockMs(self) - before;
    F(self).clock += advancedMs / 1000.0;
    Time::Timer::Fire(self, F(self).clock);
    // By index: an actor's script may create another.
    std::vector<Actor *> &actors = *F(self).actors;
    for (size_t i = 0; i < actors.size(); ++i)
        actors[i]->Update(elapsedSec, static_cast<float>(advancedMs) / 1000.0f);
    Chains::Update(self, static_cast<float>(advancedMs) / 1000.0f);
    UpdateHover(self);
}

void __fastcall EmitLayer(void *self, void *, void *batch, int layer) {
    g_class.Base<EmitLayer_t>(Offsets::VTBL_FRAME_EMIT_LAYER)(self, nullptr, batch, layer);
    if (layer == F(self).drawLayer)
        reinterpret_cast<AddCallback_t>(Offsets::FUN_RENDERBATCH_ADD_CALLBACK)(batch, nullptr,
                                                                              &Paint, self);
}

void __fastcall UpdateModel(void *self) { PlaceActors(self); }

// CSimpleFrame::Hide (0x0076AD50), which a hiding parent also calls on each
// child: the update stops, so the WMO's and the missiles' loop sounds would
// stand still.
int __fastcall Hide(void *self, void *) {
    if (World::Backdrop *world = F(self).world)
        World::Silence(*world);
    if (Terrain::Land *terrain = F(self).terrain)
        Terrain::Silence(*terrain);
    for (Actor *actor : *F(self).actors) {
        Casts::Silence(*actor);
        Areas::Silence(*actor);
        AuraVisuals::Silence(*actor);
    }
    return g_class.Base<Hide_t>(Offsets::VTBL_FRAME_HIDE)(self, nullptr);
}


// <ViewInsets> as 3.80.2 reads it (FUN_1405D6070): any of left, right, top,
// bottom set, and the four attributes are the insets (a missing one 0);
// else an <AbsInset> or <RelInset> child gives them; else a warning and 0s.
// Each <ViewInsets> replaces the last.
void ReadViewInsets(const void *node, float (&in)[4], void *status) {
    static constexpr const char *kEdges[4] = {"left", "right", "top", "bottom"};
    const auto read = [&in](const void *from) {
        bool any = false;
        for (int i = 0; i < 4; ++i) {
            in[i] = 0.0f;
            any |= Xml::FloatAttr(from, kEdges[i], in[i]);
        }
        return any;
    };
    if (read(node))
        return;
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c)) {
        if (Xml::TagIs(c, "AbsInset") || Xml::TagIs(c, "RelInset")) {
            read(c);
            return;
        }
        Xml::Warn(status, "Unknown child node in %s element: %s", "ViewInsets", Xml::Tag(c));
    }
    Xml::Warn(status, "%s element has no insets", "ViewInsets");
}

// The frame's own loader, then ModelSceneType's attributes and the first
// <FogColor> (3.80.2's 0x14194CAC0 copies fogNear and fogFar as given).
uintptr_t __fastcall LoadXML(void *layout, void *, const void *node, void *status) {
    using LayerFromName_t = int(__fastcall *)(const char *name, int *layer);
    const uintptr_t result = Frame::Subclass::Slot<LoadXML_t>(
        Offsets::VTBL_SIMPLEFRAME_LAYOUT, Offsets::VTBL_XML_NODE_LOADER)(layout, nullptr, node, status);
    if (node == nullptr)
        return result;
    void *frame = OwnerOf(layout);
    if (const char *layer = Xml::Attr(node, "drawLayer"); layer != nullptr && *layer != '\0')
        reinterpret_cast<LayerFromName_t>(Offsets::FUN_XML_DRAW_LAYER_FROM_NAME)(layer, &F(frame).drawLayer);
    Xml::FloatAttr(node, "fogNear", F(frame).fogNear);
    Xml::FloatAttr(node, "fogFar", F(frame).fogFar);
    ApplyFog(frame);
    bool fogColor = false;
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c)) {
        if (Xml::TagIs(c, "FogColor")) {
            if (!std::exchange(fogColor, true))
                Xml::ReadColor(c, Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR), status);
        } else if (Xml::TagIs(c, "ViewInsets")) {
            ReadViewInsets(c, F(frame).viewInsets, status);
        }
    }
    return result;
}

void BuildVTables() {
    if (!Frame::Subclass::BuildVTables<g_class>())
        return;
    auto fn = [](auto f) { return reinterpret_cast<const void *>(f); };
    g_class.vtable.Set(Offsets::VTBL_FRAME_SET_FRAME_ALPHA, fn(&SetFrameAlpha));
    g_class.vtable.Set(Offsets::VTBL_FRAME_ON_FRAME_UPDATE, fn(&OnFrameUpdate));
    g_class.vtable.Set(Offsets::VTBL_FRAME_EMIT_LAYER, fn(&EmitLayer));
    g_class.vtable.Set(Offsets::VTBL_FRAME_HIDE, fn(&Hide));
    g_class.vtable.Set(Offsets::VTBL_SIMPLEMODEL_UPDATE_MODEL, fn(&UpdateModel));
    g_class.layoutVtable.Set(Offsets::VTBL_XML_NODE_LOADER, fn(&LoadXML));
}

// The "ModelScene" factory, shaped like the engine's "Model" one (0x006EECF0).
void *__fastcall CreateModelScene(void *parent) {
    BuildVTables();
    void *frame = Frame::Subclass::New<g_class>(parent);
    if (frame == nullptr)
        return nullptr;
    Fields &f = F(frame);
    f.actors = new std::vector<Actor *>;
    f.position = {0.0f, 0.0f, 0.0f};
    Orientation(0.0f, 0.0f, 0.0f, f.forward, f.right, f.up);
    f.fieldOfView = kDefaultFieldOfView;
    f.nearClip = kDefaultNearClip;
    f.farClip = kDefaultFarClip;
    f.drawLayer = Offsets::DRAWLAYER_ARTWORK;
    f.groundTerrain = -1;
    f.hovered = nullptr;
    f.world = nullptr;
    f.terrain = nullptr;
    f.cameraLiquid = Offsets::LIQUID_DRY;
    f.probeDrop = 0.0f;
    f.water = nullptr;
    f.playWorldModelSounds = false;
    f.timeOfDay = -1.0f;
    f.listener = nullptr;
    f.clock = 0.0;
    f.moveMarkerShown = false;
    InitLight(frame);
    InitFog(frame);
    InstallCamera(frame);
    return frame;
}

// ---- the methods ------------------------------------------------------------------

void *Self(void *L) { return Game::Lua::ResolveTypedObject(L, 1, g_class.TypeIdVar()); }

int PushVec3(void *L, const Vec3 &v) {
    Game::Lua::PushNumber(L, v.x);
    Game::Lua::PushNumber(L, v.y);
    Game::Lua::PushNumber(L, v.z);
    return 3;
}

Vec3 Vec3Args(void *L, int first) {
    return {FloatArg(L, first), FloatArg(L, first + 1), FloatArg(L, first + 2)};
}

// Whether the template's inherits chain comes back to a template it has
// passed, CreateActor's "Recursively inherited node".
bool TemplateRecursive(const void *node) {
    std::vector<const void *> seen;
    for (; node != nullptr; node = Xml::Template(Xml::Attr(node, "inherits"))) {
        if (std::find(seen.begin(), seen.end(), node) != seen.end())
            return true;
        seen.push_back(node);
        const char *inherits = Xml::Attr(node, "inherits");
        if (inherits == nullptr || *inherits == '\0')
            return false;
    }
    return false;
}

// CreateActor (0x14194FBB0 -> 0x14194D230): the name is any string lua_isstring
// takes; a template only a string. The template loads (OnLoad included)
// before the actor joins the scene's list, so OnLoad does not find it there.
int __fastcall Script_CreateActor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const char *name = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    const void *templateNode = nullptr;
    if (Game::Lua::Type(L, 3) == Game::Lua::TYPE_STRING) {
        const char *inherits = Game::Lua::ToString(L, 3);
        templateNode = Xml::Template(inherits);
        const char *problem = templateNode == nullptr        ? "Couldn't find inherited node"
                              : TemplateRecursive(templateNode) ? "Recursively inherited node"
                                                               : nullptr;
        if (problem != nullptr) {
            char message[Game::Lua::kClientErrorSize];
            std::snprintf(message, sizeof message, "%s \"%s\"", problem, inherits);
            Game::Lua::RaiseClientMethodError(L, Game::Lua::ObjectDisplayName(frame), "CreateActor", message);
            return 0;
        }
    }
    Actor *actor = new Actor(frame, name);
    if (templateNode != nullptr)
        actor->LoadXml(templateNode, nullptr);
    F(frame).actors->push_back(actor);
    Animation::Push(L, actor);
    return 1;
}

int __fastcall Script_GetNumActors(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(F(frame).actors->size()));
    return 1;
}

// 0x140C5B0F0: a non-number is the method's own error; the index is
// lua_tointeger's; out of range returns nothing.
int __fastcall Script_GetActorAtIndex(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::RaiseClientMethodError(L, Game::Lua::ObjectDisplayName(frame), "GetActorAtIndex",
                                          "Invalid index passed");
        return 0;
    }
    const std::vector<Actor *> &actors = *F(frame).actors;
    const double index = std::trunc(Game::Lua::ToNumber(L, 2));
    if (!(index >= 1.0 && index <= static_cast<double>(actors.size())))
        return 0;
    Animation::Push(L, actors[static_cast<size_t>(index) - 1]);
    return 1;
}

// TakeActor(actor) (0x14194FE10): an actor of another scene leaves its list
// for this one's; anything else, or an actor of this scene, does nothing. An
// actor's model lives in its scene's M2 scene in 1.12, so it is cleared
// first (OnModelCleared), with the spell areas and chains of the old scene,
// as the actor's destructor drops them; the caller gives it a model again.
int __fastcall Script_TakeActor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    auto *actor = static_cast<Actor *>(Animation::Resolve(L, 2, Actor::s_class, false));
    if (actor == nullptr || actor->SceneFrame() == frame)
        return 0;
    void *old = actor->SceneFrame();
    actor->Vacate();
    Areas::Drop(*actor);
    Chains::Forget(*actor);
    Fields &from = F(old);
    from.actors->erase(std::remove(from.actors->begin(), from.actors->end(), actor), from.actors->end());
    if (from.listener == actor)
        from.listener = nullptr;
    if (from.hovered == actor)
        from.hovered = nullptr;
    actor->MoveToScene(frame);
    F(frame).actors->push_back(actor);
    return 0;
}

int __fastcall Script_SetCameraPosition(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, "Usage: self:SetCameraPosition(position)"))
        return 0;
    F(frame).position = Vec3Args(L, 2);
    ApplyCamera(frame);
    return 0;
}

int __fastcall Script_GetCameraPosition(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).position) : 0;
}

// The orthonormal test of 3.80.2's axis setter (0x14194DC20): each vector
// of squared length within 0.001 of 1 (DAT_14415B630), each pair's dot
// product within 2.38e-7 of 0 (DAT_14415B534).
bool Orthonormal(const Vec3 &a, const Vec3 &b, const Vec3 &c) {
    auto dot = [](const Vec3 &u, const Vec3 &v) { return u.x * v.x + u.y * v.y + u.z * v.z; };
    for (const Vec3 *v : {&a, &b, &c})
        if (!(std::fabs(dot(*v, *v) - 1.0f) < 0.001f))
            return false;
    return std::fabs(dot(a, b)) < kAxisEpsilon && std::fabs(dot(a, c)) < kAxisEpsilon &&
           std::fabs(dot(b, c)) < kAxisEpsilon;
}

// The axis setter: the vectors stored when they are orthonormal, else its
// error and nothing stored. YawPitchRoll goes through it too.
void SetAxes(void *L, void *frame, const Vec3 &forward, const Vec3 &right, const Vec3 &up) {
    if (!Orthonormal(forward, right, up)) {
        Game::Lua::RaiseClientMethodError(L, Game::Lua::ObjectDisplayName(frame), "SetCameraOrientationByAxisVectors",
                                          "Each vector must be orthonormal");
        return;
    }
    Fields &f = F(frame);
    f.forward = forward;
    f.right = right;
    f.up = up;
    ApplyCamera(frame);
}

int __fastcall Script_SetCameraOrientationByYawPitchRoll(void *L) {
    constexpr char kUsage[] = "Usage: self:SetCameraOrientationByYawPitchRoll(yaw, pitch, roll)";
    void *frame = Self(L);
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (frame == nullptr || !MethodArg::Number(L, 2, kUsage, yaw) || !MethodArg::Number(L, 3, kUsage, pitch) ||
        !MethodArg::Number(L, 4, kUsage, roll))
        return 0;
    Vec3 forward, right, up;
    Orientation(yaw, pitch, roll, forward, right, up);
    SetAxes(L, frame, forward, right, up);
    return 0;
}

int __fastcall Script_SetCameraOrientationByAxisVectors(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 9, "Usage: self:SetCameraOrientationByAxisVectors(forward, right, up)"))
        return 0;
    SetAxes(L, frame, Vec3Args(L, 2), Vec3Args(L, 5), Vec3Args(L, 8));
    return 0;
}

int __fastcall Script_GetCameraForward(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).forward) : 0;
}

int __fastcall Script_GetCameraRight(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).right) : 0;
}

int __fastcall Script_GetCameraUp(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).up) : 0;
}

// The one-number camera setters (0x140C5CE70, 0x140C5D050, 0x140C5CC90): a
// finite number; the field of view held to [0, 2 pi] (DAT_14415C248), a clip
// at or below 0 made 0.
constexpr char kUsageSetFieldOfView[] = "Usage: self:SetCameraFieldOfView(fov)";
constexpr char kUsageSetNearClip[] = "Usage: self:SetCameraNearClip(nearClip)";
constexpr char kUsageSetFarClip[] = "Usage: self:SetCameraFarClip(farClip)";

float ClampFieldOfView(float v) {
    constexpr float kTwoPi = 6.2831855f;
    return v > 0.0f ? (v < kTwoPi ? v : kTwoPi) : 0.0f;
}

float ClampClip(float v) { return v > 0.0f ? v : 0.0f; }

template <float Fields::*Field, const char *Usage, float (*Clamp)(float)>
int __fastcall SetCameraScalar(void *L) {
    void *frame = Self(L);
    float value = 0.0f;
    if (frame == nullptr || !MethodArg::Number(L, 2, Usage, value))
        return 0;
    F(frame).*Field = Clamp(value);
    ApplyCamera(frame);
    return 0;
}

template <float Fields::*Field>
int __fastcall GetCameraScalar(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).*Field);
    return 1;
}

int __fastcall Script_SetDrawLayer(void *L) {
    using LayerFromName_t = int(__fastcall *)(const char *name, int *layer);
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    int layer = 0;
    if (!Game::Lua::IsString(L, 2) ||
        !reinterpret_cast<LayerFromName_t>(Offsets::FUN_XML_DRAW_LAYER_FROM_NAME)(Game::Lua::ToString(L, 2), &layer))
        return MethodArg::Fail(L, 2, "Usage: self:SetDrawLayer(layer)");
    Fields &f = F(frame);
    if (layer == f.drawLayer)
        return 0;
    const int old = f.drawLayer;
    f.drawLayer = layer;
    Frame::LayerChange::NotifyFrame(frame, old);
    Frame::LayerChange::NotifyFrame(frame, layer);
    return 0;
}

int __fastcall Script_GetDrawLayer(void *L) {
    using LayerToName_t = const char *(__fastcall *)(int layer);
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushString(
        L, reinterpret_cast<LayerToName_t>(Offsets::FUN_DRAW_LAYER_TO_NAME)(F(frame).drawLayer));
    Game::Lua::PushNumber(L, 0);
    return 2;
}

// SetPaused(paused [, affectsGlobalPause]) (0x140C5EED0): each actor with a
// model at the call, as that actor's SetPaused; the scene stores nothing.
int __fastcall Script_SetPaused(void *L) {
    constexpr char kUsage[] = "Usage: self:SetPaused(paused [, affectsGlobalPause])";
    void *frame = Self(L);
    bool paused = false;
    if (frame == nullptr || !MethodArg::Bool(L, 2, kUsage, paused))
        return 0;
    const bool affectsGlobalPause = MethodArg::OptBool(L, 3, true);
    for (Actor *actor : *F(frame).actors)
        if (actor->model != nullptr)
            actor->Pause(paused, affectsGlobalPause);
    return 0;
}

int __fastcall Script_GetSceneTime(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).clock);
    return 1;
}

template <Time::Timer::Kind kind>
int __fastcall Script_Timer(void *L) {
    static constexpr const char *kUsage[] = {
        "Usage: scene:After(seconds, callback)",
        "Usage: local cbObject = scene:NewTimer(seconds, callback)",
        "Usage: local cbObject = scene:NewTicker(seconds, callback [, iterations])",
    };
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    return Time::Timer::Script(L, 2, frame, F(frame).clock, kind, kUsage[static_cast<int>(kind)]);
}

int __fastcall Script_Project3DPointTo2D(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, "Usage: self:Project3DPointTo2D(pointX, pointY, pointZ)"))
        return 0;
    double out[3];
    if (!Project(frame, Vec3Args(L, 2), out))
        return 0;
    for (double value : out)
        Game::Lua::PushNumber(L, value);
    return 3;
}

int __fastcall Script_GetActorAtCursor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Actor *actor = ActorAtCursor(frame);
    if (actor == nullptr)
        return 0;
    Animation::Push(L, actor);
    return 1;
}

int __fastcall Script_GetCursorRay(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    float nearPoint[3], farPoint[3];
    if (!CursorRay(frame, nearPoint, farPoint))
        return 0;
    for (float value : nearPoint)
        Game::Lua::PushNumber(L, value);
    for (float value : farPoint)
        Game::Lua::PushNumber(L, value);
    return 6;
}

// The client's nearest-unit collector (0x00494200, for TargetNearest*) on the
// scene's actors: a shown actor with a model (the collector keeps a unit
// whose scene node is drawn, 0x006704C0) whose squared distance from the
// point, in three axes, is not more than the squared radius; the list goes
// nearest first, as its sort (0x00494450) orders a kind. Ties keep creation
// order.
int __fastcall Script_GetActorsInRadius(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 4, "Usage: self:GetActorsInRadius(pointX, pointY, pointZ, radius)"))
        return 0;
    const Vec3 point = Vec3Args(L, 2);
    const float radius = FloatArg(L, 5);
    struct Hit {
        float distanceSq;
        Actor *actor;
    };
    std::vector<Hit> hits;
    for (Actor *actor : *F(frame).actors) {
        if (!actor->shown || actor->model == nullptr)
            continue;
        const Actor::Point origin = actor->Origin();
        const float dx = origin.p[0] - point.x;
        const float dy = origin.p[1] - point.y;
        const float dz = origin.p[2] - point.z;
        const float distanceSq = dx * dx + dy * dy + dz * dz;
        if (distanceSq <= radius * radius)
            hits.push_back({distanceSq, actor});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit &a, const Hit &b) { return a.distanceSq < b.distanceSq; });
    Game::Lua::NewTable(L);
    int n = 0;
    for (const Hit &hit : hits) {
        Animation::Push(L, hit.actor);
        Game::Lua::RawSetI(L, -2, ++n);
    }
    return 1;
}

int __fastcall Script_SetMoveMarker(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, "Usage: self:SetMoveMarker(pointX, pointY, pointZ)"))
        return 0;
    Fields &f = F(frame);
    for (int i = 0; i < 3; ++i)
        f.moveMarker[i] = FloatArg(L, 2 + i);
    f.moveMarkerShown = true;
    return 0;
}

int __fastcall Script_ClearMoveMarker(void *L) {
    if (void *frame = Self(L))
        F(frame).moveMarkerShown = false;
    return 0;
}

int __fastcall Script_GetMoveMarker(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !F(frame).moveMarkerShown)
        return 0;
    for (float v : F(frame).moveMarker)
        Game::Lua::PushNumber(L, v);
    return 3;
}

int __fastcall Script_SetViewInsets(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 4, "Usage: self:SetViewInsets(left, right, top, bottom)"))
        return 0;
    float *in = F(frame).viewInsets;
    for (int i = 0; i < 4; ++i)
        in[i] = FloatArg(L, 2 + i);
    return 0;
}

int __fastcall Script_GetViewInsets(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    for (float value : F(frame).viewInsets)
        Game::Lua::PushNumber(L, value);
    return 4;
}

int __fastcall Script_SetViewTranslation(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 2, "Usage: self:SetViewTranslation(translationX, translationY)"))
        return 0;
    F(frame).viewTranslation[0] = FloatArg(L, 2);
    F(frame).viewTranslation[1] = FloatArg(L, 3);
    return 0;
}

int __fastcall Script_GetViewTranslation(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).viewTranslation[0]);
    Game::Lua::PushNumber(L, F(frame).viewTranslation[1]);
    return 2;
}

// The ground the actors' footsteps and death thuds sound on, as a unit's
// terrain type (+0xC60) chooses them in the world. nil: no ground.
int __fastcall Script_SetGroundTerrain(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL) {
        F(frame).groundTerrain = -1;
        return 0;
    }
    if (!NumberArgs(L, 2, 1, "Usage: self:SetGroundTerrain([terrainTypeID])"))
        return 0;
    F(frame).groundTerrain = static_cast<int32_t>(Game::Lua::ToNumber(L, 2));
    return 0;
}

int __fastcall Script_GetGroundTerrain(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || F(frame).groundTerrain < 0)
        return 0;
    Game::Lua::PushNumber(L, F(frame).groundTerrain);
    return 1;
}

// The scene's own WMO (a ClassicAPI extension, model/SceneWorld.cpp), beside
// the terrain's. A set that fails clears it, as an actor's SetModelByPath
// clears its model.
constexpr char kUsageSetWorldModel[] = "Usage: self:SetWorldModel(path [, x, y, z [, yaw [, doodadSet [, mapID]]]])";

int __fastcall Script_SetWorldModel(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2))
        return MethodArg::Fail(L, 2, kUsageSetWorldModel);
    const auto given = [L](int index) { return Game::Lua::Type(L, index) > Game::Lua::TYPE_NIL; };
    float position[3] = {};
    if (given(3) || given(4) || given(5)) {
        if (!NumberArgs(L, 3, 3, kUsageSetWorldModel))
            return 0;
        position[0] = FloatArg(L, 3);
        position[1] = FloatArg(L, 4);
        position[2] = FloatArg(L, 5);
    }
    for (int index = 6; index <= 8; ++index)
        if (given(index) && !NumberArgs(L, index, 1, kUsageSetWorldModel))
            return 0;
    const float yaw = given(6) ? FloatArg(L, 6) : 0.0f;
    const uint32_t doodadSet = given(7) ? static_cast<uint32_t>(Game::Lua::ToNumber(L, 7)) : 0;
    const uint32_t mapID = given(8) ? static_cast<uint32_t>(Game::Lua::ToNumber(L, 8)) : World::kNoMap;
    float placement[16];
    World::Placement(position, yaw, placement);
    Game::Lua::PushBool(L, World::SetModel(F(frame).world, Game::Lua::ToString(L, 2), placement, doodadSet, mapID));
    return 1;
}

int __fastcall Script_SetWorldModelByMapID(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 1, "Usage: self:SetWorldModelByMapID(mapID)"))
        return 0;
    Game::Lua::PushBool(L, World::SetMapModel(F(frame).world, static_cast<uint32_t>(Game::Lua::ToNumber(L, 2))));
    return 1;
}

int __fastcall Script_ClearWorldModel(void *L) {
    if (void *frame = Self(L))
        World::ClearModel(F(frame).world);
    return 0;
}

// The scene's terrain (a ClassicAPI extension, model/SceneTerrain.cpp), with
// no chunk until the scene's update streams them. A set that fails clears
// it, as SetWorldModel does. The old terrain's WMOs go with it (a unique id
// names a def only within its map).
void SetTerrain(void *frame, Terrain::Land *terrain) {
    Terrain::Free(F(frame).terrain);
    F(frame).terrain = terrain;
    World::SetTileObjects(F(frame).world, {}, World::kNoMap);
}

constexpr char kUsageSetWorldTerrain[] = "Usage: self:SetWorldTerrain(mapID)";

int __fastcall Script_SetWorldTerrain(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (!NumberArgs(L, 2, 1, kUsageSetWorldTerrain))
        return 0;
    SetTerrain(frame, Terrain::Open(static_cast<uint32_t>(Game::Lua::ToNumber(L, 2))));
    Game::Lua::PushBool(L, F(frame).terrain != nullptr);
    return 1;
}

int __fastcall Script_ClearWorldTerrain(void *L) {
    if (void *frame = Self(L))
        SetTerrain(frame, nullptr);
    return 0;
}

// The nearest face of the scene's ground on a segment (a ClassicAPI
// extension): the hit point, the face's normal toward the start, and the
// fraction. Nothing on a miss or without ground.
int __fastcall Script_TraceSegment(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 6, "Usage: self:TraceSegment(startX, startY, startZ, endX, endY, endZ [, camera])"))
        return 0;
    const float start[3] = {FloatArg(L, 2), FloatArg(L, 3), FloatArg(L, 4)};
    const float end[3] = {FloatArg(L, 5), FloatArg(L, 6), FloatArg(L, 7)};
    float fraction = 1.0f;
    float normal[3];
    const bool camera = Game::Lua::ToBoolean(L, 8) != 0;
    if (!TraceGround(frame, start, end, camera, fraction, normal))
        return 0;
    for (int i = 0; i < 3; ++i)
        Game::Lua::PushNumber(L, start[i] + (end[i] - start[i]) * fraction);
    for (float c : normal)
        Game::Lua::PushNumber(L, c);
    Game::Lua::PushNumber(L, fraction);
    return 7;
}

// The liquid at a point of the scene's WMO or terrain (a ClassicAPI
// extension): its type and its surface's z, nil for a WMO group that is
// liquid throughout. Nothing when the point is dry.
int __fastcall Script_GetLiquidAtPoint(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 3, "Usage: self:GetLiquidAtPoint(pointX, pointY, pointZ)"))
        return 0;
    const float point[3] = {FloatArg(L, 2), FloatArg(L, 3), FloatArg(L, 4)};
    uint32_t type = 0;
    float surface = 0.0f;
    if (!LiquidAtPoint(frame, point, type, surface))
        return 0;
    Game::Lua::PushNumber(L, type);
    if (surface >= FLT_MAX)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushNumber(L, surface);
    return 2;
}

// Sweeps a body by `move` to its first contact with the scene's ground, as
// the client's mover sweeps a unit's volume (CMovement::SweepVolume
// 0x00632BA0, SceneContact.h): a box `radius` from `feet` on each side and
// `height` tall over a downward point at `feet`. `end` is where the feet
// stop; true when a face was met, `normal` the facet's plane normal. With
// no ground the body moves the full distance; the feet stay put while the
// WMO is loading.
bool SweepGround(void *frame, const float (&feet)[3], float radius, float height, const float (&move)[3],
                 float (&end)[3], float (&normal)[3]) {
    normal[0] = normal[1] = normal[2] = 0.0f;
    if (!HasGround(frame)) {
        for (int k = 0; k < 3; ++k)
            end[k] = feet[k] + move[k];
        return false;
    }
    std::copy(std::begin(feet), std::end(feet), std::begin(end));
    if (!(radius > 0.0f) || !(height > 0.0f))
        return false;
    // The move as the substep loop (0x00634040) hands it on: its length at
    // 53 bits, the unit direction by the reciprocal, none below 2^-20.
    const double length = std::sqrt((static_cast<double>(move[0]) * move[0] + static_cast<double>(move[1]) * move[1]) +
                                    static_cast<double>(move[2]) * move[2]);
    float dir[3] = {0.0f, 0.0f, 0.0f};
    if (!(length < 9.5367431640625e-7)) {
        const double inverse = 1.0 / length;
        for (int k = 0; k < 3; ++k)
            dir[k] = static_cast<float>(move[k] * inverse);
    }
    // The facets start empty over the volume's own box, as the substep's
    // query frame (0x00633840) leaves its cache; the sweep's gather then
    // takes that box joined with the end of the move.
    Contact::Facets facets{{}, {}, {}, &QueryGroundFacets, &QueryGroundLiquidFacets, frame};
    Contact::BaseBox(feet, radius, height, facets.box);
    Contact::Contact contact;
    if (!Contact::Sweep(facets, false, false, feet, radius, height, dir, static_cast<float>(length), contact))
        return false;
    for (int k = 0; k < 3; ++k)
        end[k] = static_cast<float>(feet[k] + static_cast<double>(dir[k]) * contact.distance);
    if (contact.hit >= facets.solid.size())
        return false;
    std::copy(std::begin(facets.solid[contact.hit].plane.n), std::end(facets.solid[contact.hit].plane.n),
              std::begin(normal));
    return true;
}

// A body swept through the scene's ground to its first contact (a ClassicAPI
// extension): where its feet stop, and the normal of the face it met.
int __fastcall Script_SweepCollider(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 8, "Usage: self:SweepCollider(feetX, feetY, feetZ, radius, height, moveX, moveY, moveZ)"))
        return 0;
    const float feet[3] = {FloatArg(L, 2), FloatArg(L, 3), FloatArg(L, 4)};
    const float move[3] = {FloatArg(L, 7), FloatArg(L, 8), FloatArg(L, 9)};
    float end[3];
    float normal[3];
    const bool hit = SweepGround(frame, feet, FloatArg(L, 5), FloatArg(L, 6), move, end, normal);
    for (float c : end)
        Game::Lua::PushNumber(L, c);
    if (!hit)
        return 3;
    for (float c : normal)
        Game::Lua::PushNumber(L, c);
    return 6;
}

// The actor the scene's sounds are heard at, as the world hears them at the
// unit the player drives (a ClassicAPI extension). nil: the camera again.
int __fastcall Script_SetSoundListenerActor(void *L) {
    constexpr char kUsage[] = "Usage: self:SetSoundListenerActor([actor])";
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL) {
        F(frame).listener = nullptr;
        return 0;
    }
    auto *actor = static_cast<Actor *>(Animation::Resolve(L, 2, Actor::s_class, false));
    if (actor == nullptr || actor->SceneFrame() != frame)
        return MethodArg::Fail(L, 2, kUsage);
    F(frame).listener = actor;
    return 0;
}

int __fastcall Script_GetSoundListenerActor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || F(frame).listener == nullptr)
        return 0;
    Animation::Push(L, F(frame).listener);
    return 1;
}

// The next update drops the loops when it is turned off.
int __fastcall Script_SetPlayWorldModelSounds(void *L) {
    if (void *frame = Self(L))
        F(frame).playWorldModelSounds = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __fastcall Script_GetPlayWorldModelSounds(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).playWorldModelSounds);
    return 1;
}

// The time of day the scene's world is lit at (a ClassicAPI extension), as a
// share of the day; any number wraps into [0, 1).
int __fastcall Script_SetWorldTimeOfDay(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 1, "Usage: self:SetWorldTimeOfDay(timeOfDay)"))
        return 0;
    const double t = Game::Lua::ToNumber(L, 2);
    const float day = static_cast<float>(t - std::floor(t));
    F(frame).timeOfDay = day < 1.0f ? day : 0.0f;
    return 0;
}

int __fastcall Script_ClearWorldTimeOfDay(void *L) {
    if (void *frame = Self(L))
        F(frame).timeOfDay = -1.0f;
    return 0;
}

int __fastcall Script_GetWorldTimeOfDay(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || F(frame).timeOfDay < 0.0f)
        return 0;
    Game::Lua::PushNumber(L, F(frame).timeOfDay);
    return 1;
}

// A required bool (0x140C5CAB0): nil raises the usage error.
int __fastcall Script_SetAllowOverlappedModels(void *L) {
    void *frame = Self(L);
    bool allow = false;
    if (frame == nullptr ||
        !MethodArg::Bool(L, 2, "Usage: self:SetAllowOverlappedModels(allowOverlappedModels)", allow))
        return 0;
    F(frame).allowOverlappedModels = allow ? 1 : 0;
    return 0;
}

// 0x140C5D910: each actor that exists now takes the strength; the scene
// keeps none, so actors made later stay in color.
int __fastcall Script_SetDesaturation(void *L) {
    void *frame = Self(L);
    float amount = 0.0f;
    if (frame == nullptr || !MethodArg::Number(L, 2, "Usage: self:SetDesaturation(strength)", amount))
        return 0;
    for (Actor *actor : *F(frame).actors)
        actor->SetDesaturation(amount);
    return 0;
}

int __fastcall Script_GetAllowOverlappedModels(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).allowOverlappedModels != 0);
    return 1;
}

// The light type reader (0x140C62970): a number whose lua_tointeger, as an
// unsigned value, is at most 1 (0 Directional, 1 Point); else the usage
// error.
int __fastcall Script_SetLightType(void *L) {
    constexpr char kUsage[] = "Usage: self:SetLightType(lightType)";
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2))
        return MethodArg::Fail(L, 2, kUsage);
    const double v = std::trunc(Game::Lua::ToNumber(L, 2));
    if (!(v >= 0.0 && v <= 1.0))
        return MethodArg::Fail(L, 2, kUsage);
    SetLightType(frame, static_cast<uint32_t>(v));
    return 0;
}

int __fastcall Script_GetLightType(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).lightType);
    return 1;
}

int __fastcall Script_SetLightVisible(void *L) {
    if (void *frame = Self(L))
        SetLightVisible(frame, MethodArg::OptBool(L, 2, false));
    return 0;
}

int __fastcall Script_IsLightVisible(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).lightVisible != 0);
    return 1;
}

int __fastcall Script_SetLightDirection(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, "Usage: self:SetLightDirection(direction)"))
        return 0;
    SetLightDirection(frame, Vec3Args(L, 2));
    return 0;
}

int __fastcall Script_GetLightDirection(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).lightDirection) : 0;
}

int __fastcall Script_SetLightPosition(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, "Usage: self:SetLightPosition(position)"))
        return 0;
    SetLightPosition(frame, Vec3Args(L, 2));
    return 0;
}

int __fastcall Script_GetLightPosition(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).lightPosition) : 0;
}

// The colour reader (0x140A6CC50) clamps and quantizes each channel, so the
// light keeps, and the getters give back, the 1/255 steps (0x140C5E280,
// 0x140C5BE70).
int SetLightColor(void *L, float *(*color)(void *), const char *usage) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, usage))
        return 0;
    SetColor(color(frame), QuantizedColor(Vec3Args(L, 2)));
    return 0;
}

int GetLightColor(void *L, float *(*color)(void *)) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const float *c = color(frame);
    return PushVec3(L, {c[0], c[1], c[2]});
}

int __fastcall Script_SetLightAmbientColor(void *L) {
    return SetLightColor(L, &AmbientOf, "Usage: self:SetLightAmbientColor(color)");
}
int __fastcall Script_GetLightAmbientColor(void *L) { return GetLightColor(L, &AmbientOf); }
int __fastcall Script_SetLightDiffuseColor(void *L) {
    return SetLightColor(L, &DiffuseOf, "Usage: self:SetLightDiffuseColor(color)");
}
int __fastcall Script_GetLightDiffuseColor(void *L) { return GetLightColor(L, &DiffuseOf); }

int __fastcall Script_SetFogColor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, "Usage: self:SetFogColor(color)"))
        return 0;
    SetFogColor(frame, Vec3Args(L, 2));
    return 0;
}

int __fastcall Script_GetFogColor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const uint32_t c = Game::Read<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR);
    return PushVec3(L, {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f});
}

// SetFogNear / SetFogFar (0x140C5E0B0, 0x140C5DEE0): a finite number, stored
// as given.
template <float Fields::*Field, const char *Usage>
int __fastcall SetFogDistance(void *L) {
    void *frame = Self(L);
    float value = 0.0f;
    if (frame == nullptr || !MethodArg::Number(L, 2, Usage, value))
        return 0;
    F(frame).*Field = value;
    ApplyFog(frame);
    return 0;
}

template <float Fields::*Field>
int __fastcall GetFogDistance(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).*Field);
    return 1;
}

constexpr char kUsageSetFogNear[] = "Usage: self:SetFogNear(near)";
constexpr char kUsageSetFogFar[] = "Usage: self:SetFogFar(far)";

// ClearFog (0x140C5AFF0): the near and far back to the constructor's, the
// colour left.
int __fastcall Script_ClearFog(void *L) {
    if (void *frame = Self(L)) {
        F(frame).fogNear = kNoFogNear;
        F(frame).fogFar = kNoFogFar;
        ApplyFog(frame);
    }
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"CreateActor", &Script_CreateActor},
    {"GetNumActors", &Script_GetNumActors},
    {"GetActorAtIndex", &Script_GetActorAtIndex},
    {"TakeActor", &Script_TakeActor},
    {"SetCameraPosition", &Script_SetCameraPosition},
    {"GetCameraPosition", &Script_GetCameraPosition},
    {"SetCameraOrientationByYawPitchRoll", &Script_SetCameraOrientationByYawPitchRoll},
    {"SetCameraOrientationByAxisVectors", &Script_SetCameraOrientationByAxisVectors},
    {"GetCameraForward", &Script_GetCameraForward},
    {"GetCameraRight", &Script_GetCameraRight},
    {"GetCameraUp", &Script_GetCameraUp},
    {"SetCameraFieldOfView", &SetCameraScalar<&Fields::fieldOfView, kUsageSetFieldOfView, &ClampFieldOfView>},
    {"GetCameraFieldOfView", &GetCameraScalar<&Fields::fieldOfView>},
    {"SetCameraNearClip", &SetCameraScalar<&Fields::nearClip, kUsageSetNearClip, &ClampClip>},
    {"GetCameraNearClip", &GetCameraScalar<&Fields::nearClip>},
    {"SetCameraFarClip", &SetCameraScalar<&Fields::farClip, kUsageSetFarClip, &ClampClip>},
    {"GetCameraFarClip", &GetCameraScalar<&Fields::farClip>},
    {"SetDrawLayer", &Script_SetDrawLayer},
    {"GetDrawLayer", &Script_GetDrawLayer},
    {"SetPaused", &Script_SetPaused},
    {"GetSceneTime", &Script_GetSceneTime},
    {"After", &Script_Timer<Time::Timer::Kind::After>},
    {"NewTimer", &Script_Timer<Time::Timer::Kind::NewTimer>},
    {"NewTicker", &Script_Timer<Time::Timer::Kind::NewTicker>},
    {"SetGroundTerrain", &Script_SetGroundTerrain},
    {"GetGroundTerrain", &Script_GetGroundTerrain},
    {"SetWorldModel", &Script_SetWorldModel},
    {"SetWorldModelByMapID", &Script_SetWorldModelByMapID},
    {"ClearWorldModel", &Script_ClearWorldModel},
    {"SetWorldTerrain", &Script_SetWorldTerrain},
    {"ClearWorldTerrain", &Script_ClearWorldTerrain},
    {"TraceSegment", &Script_TraceSegment},
    {"GetLiquidAtPoint", &Script_GetLiquidAtPoint},
    {"SweepCollider", &Script_SweepCollider},
    {"SetSoundListenerActor", &Script_SetSoundListenerActor},
    {"GetSoundListenerActor", &Script_GetSoundListenerActor},
    {"SetPlayWorldModelSounds", &Script_SetPlayWorldModelSounds},
    {"GetPlayWorldModelSounds", &Script_GetPlayWorldModelSounds},
    {"SetWorldTimeOfDay", &Script_SetWorldTimeOfDay},
    {"ClearWorldTimeOfDay", &Script_ClearWorldTimeOfDay},
    {"GetWorldTimeOfDay", &Script_GetWorldTimeOfDay},
    {"SetAllowOverlappedModels", &Script_SetAllowOverlappedModels},
    {"SetDesaturation", &Script_SetDesaturation},
    {"GetAllowOverlappedModels", &Script_GetAllowOverlappedModels},
    {"Project3DPointTo2D", &Script_Project3DPointTo2D},
    {"GetActorAtCursor", &Script_GetActorAtCursor},
    {"GetCursorRay", &Script_GetCursorRay},
    {"GetActorsInRadius", &Script_GetActorsInRadius},
    {"SetMoveMarker", &Script_SetMoveMarker},
    {"ClearMoveMarker", &Script_ClearMoveMarker},
    {"GetMoveMarker", &Script_GetMoveMarker},
    {"SetViewInsets", &Script_SetViewInsets},
    {"GetViewInsets", &Script_GetViewInsets},
    {"SetViewTranslation", &Script_SetViewTranslation},
    {"GetViewTranslation", &Script_GetViewTranslation},
    {"SetLightType", &Script_SetLightType},
    {"GetLightType", &Script_GetLightType},
    {"SetLightVisible", &Script_SetLightVisible},
    {"IsLightVisible", &Script_IsLightVisible},
    {"SetLightDirection", &Script_SetLightDirection},
    {"GetLightDirection", &Script_GetLightDirection},
    {"SetLightPosition", &Script_SetLightPosition},
    {"GetLightPosition", &Script_GetLightPosition},
    {"SetLightAmbientColor", &Script_SetLightAmbientColor},
    {"GetLightAmbientColor", &Script_GetLightAmbientColor},
    {"SetLightDiffuseColor", &Script_SetLightDiffuseColor},
    {"GetLightDiffuseColor", &Script_GetLightDiffuseColor},
    {"SetFogColor", &Script_SetFogColor},
    {"GetFogColor", &Script_GetFogColor},
    {"SetFogNear", &SetFogDistance<&Fields::fogNear, kUsageSetFogNear>},
    {"GetFogNear", &GetFogDistance<&Fields::fogNear>},
    {"SetFogFar", &SetFogDistance<&Fields::fogFar, kUsageSetFogFar>},
    {"GetFogFar", &GetFogDistance<&Fields::fogFar>},
    {"ClearFog", &Script_ClearFog},
};

// ---- documentation --------------------------------------------------------------

using Game::Doc::Opt;
using Game::Doc::Req;

const Game::Doc::Field kCreateActorArgs[] = {
    Opt("name", "cstring", nullptr, "A global name for the actor; \"$parent\" stands for the scene's."),
    Opt("template", "cstring"),
};
const Game::Doc::Field kActorRet[] = {Req("actor", "ModelSceneFrameActor")};
const Game::Doc::Field kNumActorsRet[] = {Req("numActors", "number")};
const Game::Doc::Field kIndexArg[] = {Req("index", "luaIndex")};
const Game::Doc::Field kPositionArgs[] = {Req("positionX", "number"), Req("positionY", "number"),
                                          Req("positionZ", "number")};
const Game::Doc::Field kPositionRets[] = {Req("positionX", "number"), Req("positionY", "number"),
                                          Req("positionZ", "number")};
const Game::Doc::Field kYawPitchRollArgs[] = {
    Req("yaw", "number", "Radians about the up axis."),
    Req("pitch", "number", "Radians; positive looks down."),
    Req("roll", "number", "Radians about the forward axis."),
};
const Game::Doc::Field kAxisArgs[] = {
    Req("forwardX", "number"), Req("forwardY", "number"), Req("forwardZ", "number"),
    Req("rightX", "number"),   Req("rightY", "number"),   Req("rightZ", "number"),
    Req("upX", "number"),      Req("upY", "number"),      Req("upZ", "number"),
};
const Game::Doc::Field kForwardRets[] = {Req("forwardX", "number"), Req("forwardY", "number"),
                                         Req("forwardZ", "number")};
const Game::Doc::Field kRightRets[] = {Req("rightX", "number"), Req("rightY", "number"),
                                       Req("rightZ", "number")};
const Game::Doc::Field kUpRets[] = {Req("upX", "number"), Req("upY", "number"),
                                    Req("upZ", "number")};
const Game::Doc::Field kFovArg[] = {Req("fov", "number", "Radians.")};
const Game::Doc::Field kFovRet[] = {Req("fov", "number")};
const Game::Doc::Field kNearArg[] = {Req("nearClip", "number")};
const Game::Doc::Field kNearRet[] = {Req("nearClip", "number")};
const Game::Doc::Field kFarArg[] = {Req("farClip", "number")};
const Game::Doc::Field kFarRet[] = {Req("farClip", "number")};

const Game::Doc::Function kCreateActor{"Adds an actor to the scene.", kCreateActorArgs, kActorRet};
const Game::Doc::Function kGetNumActors{"Returns how many actors the scene has.", {},
                                        kNumActorsRet};
const Game::Doc::Function kGetActorAtIndex{"Returns an actor, in creation order.", kIndexArg,
                                           kActorRet};
const Game::Doc::Field kTakeActorArg[] = {Req("actor", "ModelSceneFrameActor")};
const Game::Doc::Function kTakeActor{
    "Moves an actor of another scene into this scene. The actor's model is cleared; give it a model again. "
    "Does nothing for an actor of this scene.",
    kTakeActorArg, {}};
const Game::Doc::Function kSetCameraPosition{"Moves the camera.", kPositionArgs, {}};
const Game::Doc::Function kGetCameraPosition{"Returns where the camera is.", {}, kPositionRets};
const Game::Doc::Function kSetCameraOrientationByYawPitchRoll{
    "Points the camera by yaw, pitch and roll.", kYawPitchRollArgs, {}};
const Game::Doc::Function kSetCameraOrientationByAxisVectors{
    "Points the camera by its forward, right and up axes.", kAxisArgs, {}};
const Game::Doc::Function kGetCameraForward{"Returns the direction the camera looks.", {},
                                            kForwardRets};
const Game::Doc::Function kGetCameraRight{"Returns the camera's right axis.", {}, kRightRets};
const Game::Doc::Function kGetCameraUp{"Returns the camera's up axis.", {}, kUpRets};
const Game::Doc::Function kSetCameraFieldOfView{"Sets the camera's field of view.", kFovArg, {}};
const Game::Doc::Function kGetCameraFieldOfView{"Returns the camera's field of view.", {},
                                                kFovRet};
const Game::Doc::Function kSetCameraNearClip{"Sets the nearest distance the camera draws.",
                                             kNearArg, {}};
const Game::Doc::Function kGetCameraNearClip{"Returns the nearest distance the camera draws.", {},
                                             kNearRet};
const Game::Doc::Function kSetCameraFarClip{"Sets the farthest distance the camera draws.",
                                            kFarArg, {}};
const Game::Doc::Function kGetCameraFarClip{"Returns the farthest distance the camera draws.", {},
                                            kFarRet};

const Game::Doc::Field kLayerArg[] = {Req("layer", "DrawLayer")};
const Game::Doc::Field kLayerRets[] = {Req("layer", "DrawLayer"), Req("sublevel", "number")};
const Game::Doc::Field kPausedArgs[] = {
    Req("paused", "bool"),
    Opt("affectsGlobalPause", "bool", "true"),
};
const Game::Doc::Field kLightTypeArg[] = {Req("lightType", "ModelLightType")};
const Game::Doc::Field kLightTypeRet[] = {Req("lightType", "ModelLightType")};
const Game::Doc::Field kVisibleArg[] = {Opt("visible", "bool", "false")};
const Game::Doc::Field kVisibleRet[] = {Req("isVisible", "bool")};
const Game::Doc::Field kDirectionArgs[] = {Req("directionX", "number"), Req("directionY", "number"),
                                           Req("directionZ", "number")};
const Game::Doc::Field kDirectionRets[] = {Req("directionX", "number"), Req("directionY", "number"),
                                           Req("directionZ", "number")};
const Game::Doc::Field kColorArgs[] = {Req("colorR", "number"), Req("colorG", "number"),
                                       Req("colorB", "number")};
const Game::Doc::Field kColorRets[] = {Req("colorR", "number"), Req("colorG", "number"),
                                       Req("colorB", "number")};
const Game::Doc::Field kNearFogArg[] = {Req("near", "number")};
const Game::Doc::Field kNearFogRet[] = {Req("near", "number")};
const Game::Doc::Field kFarFogArg[] = {Req("far", "number")};
const Game::Doc::Field kFarFogRet[] = {Req("far", "number")};

const Game::Doc::Function kSetDrawLayer{"Sets the layer the scene draws in.", kLayerArg, {}};
const Game::Doc::Function kGetDrawLayer{"Returns the layer the scene draws in.", {}, kLayerRets};
const Game::Doc::Function kSetPaused{
    "Pauses or unpauses each actor of the scene that has a model, as the actor's own SetPaused does. Actors "
    "made later, and actors without a model, are not changed.",
    kPausedArgs, {}};
const Game::Doc::Field kSceneTimeRet[] = {Req("seconds", "number")};
const Game::Doc::Function kGetSceneTime{
    "Returns the seconds that the clock of the scene has run since the scene was made. The clock "
    "stops while the scene is hidden. The actors animate and move by this clock.",
    {}, kSceneTimeRet, nullptr, true};
const Game::Doc::Field kTimerArgs[] = {Req("seconds", "number", "On the clock of the scene."),
                                       Req("callback", "function")};
const Game::Doc::Field kTickerArgs[] = {Req("seconds", "number", "On the clock of the scene."),
                                        Req("callback", "function"),
                                        Opt("iterations", "number", nullptr, "Without it, the ticker runs until it is cancelled.")};
const Game::Doc::Field kTimerRet[] = {
    Req("cbObject", "table", "Cancel() stops the timer. IsCancelled() returns true when the timer has stopped.")};
const Game::Doc::Function kAfter{
    "Calls the callback once, after the clock of the scene has run for the seconds. As C_Timer.After, "
    "on the clock of the scene.",
    kTimerArgs, {}, nullptr, true};
const Game::Doc::Function kNewTimer{
    "Calls a function once after a delay, as scene:After does, and returns a handle that "
    "can cancel the timer.",
    kTimerArgs, kTimerRet, nullptr, true};
const Game::Doc::Function kNewTicker{
    "Calls the callback each time the clock of the scene runs for the seconds. As C_Timer.NewTicker, "
    "on the clock of the scene.",
    kTickerArgs, kTimerRet, nullptr, true};
const Game::Doc::Field kGroundTerrainArg[] = {
    Opt("terrainTypeID", "number", nullptr,
        "A TerrainType.dbc row: 0 Dirt, 1 Metallic, 2 Stone, 3 Snow, 4 Wood, 5 Grass, 6 Leaves, 7 "
        "Sand, 8 Soggy, 9 DustyGrass. Without it, the scene has no ground."),
};
const Game::Doc::Field kGroundTerrainRet[] = {Opt("terrainTypeID", "number")};
const Game::Doc::Function kSetGroundTerrain{
    "Sets the ground that the footsteps and death thuds of the actors sound on.",
    kGroundTerrainArg, {}, nullptr, true};
const Game::Doc::Function kGetGroundTerrain{
    "Returns the ground terrain of the scene, or nothing when it has none.", {},
    kGroundTerrainRet, nullptr, true};
const Game::Doc::Field kWorldModelArgs[] = {
    Req("path", "cstring", "A .wmo file, for example \"World\\wmo\\Dungeon\\KL_OnyxiasLair\\KL_OnyxiasLair_B.wmo\"."),
    Opt("positionX", "number", "0"),
    Opt("positionY", "number", "0"),
    Opt("positionZ", "number", "0"),
    Opt("yaw", "number", "0", "Radians about the up axis."),
    Opt("doodadSet", "number", "0"),
    Opt("mapID", "number", nullptr,
        "The Map.dbc row the WMO stands on: its outdoor water takes that map's light at the camera. Without it, "
        "the light where the player is."),
};
const Game::Doc::Field kWorldModelMapArg[] = {
    Req("mapID", "number", "A Map.dbc row whose map is one WMO, for example 249 (Onyxia's Lair)."),
};
const Game::Doc::Field kWorldModelRet[] = {Req("success", "bool")};
const Game::Doc::Function kSetWorldModel{
    "Shows a WMO around the actors, lit and fogged by its own data the way the world shows it. The "
    "scene's light stands in for the sun, unless SetWorldTimeOfDay lights a map's WMO as the world is lit. A WMO with fewer than two fogs takes the scene's fog. Returns "
    "false, and shows no WMO, when the file is missing or is not a WMO.",
    kWorldModelArgs, kWorldModelRet, nullptr, true};
const Game::Doc::Function kSetWorldModelByMapID{
    "Shows the WMO of a map that is one WMO, placed as the map places it, so scene positions are the "
    "map's positions. Returns false, and shows no WMO, for any other map.",
    kWorldModelMapArg, kWorldModelRet, nullptr, true};
const Game::Doc::Function kClearWorldModel{"Removes the scene's WMO.", {}, {}, nullptr, true};
const Game::Doc::Field kWorldTerrainArgs[] = {
    Req("mapID", "number", "A Map.dbc row with terrain, for example 0 (Eastern Kingdoms)."),
};
const Game::Doc::Function kSetWorldTerrain{
    "Shows the map's ground the way the world draws it, with its textures, shadows and level of detail, in "
    "map coordinates. The ground follows the scene's camera as the world's follows its own: what lies within "
    "the camera's far clip loads at once, a ring past it loads a little each frame, and ground farther away "
    "is let go. The scene's light stands in for the sun, and its fog for the zone's, unless SetWorldTimeOfDay "
    "lights it as the world is lit. The map's sky shows behind it. Actors walk on it, and TraceSegment and SweepCollider find it where it has loaded. The WMOs "
    "the map places on it (buildings, cities) show too, each as SetWorldModel shows one. The map's props on "
    "it (trees, plants, rocks) show too, and actors collide with them; its grass shows near the camera, and "
    "its water and lava show; actors swim in the water, and under it the camera takes the map's underwater "
    "fog. Returns false, and shows no terrain, for a map without terrain (a map that is one WMO).",
    kWorldTerrainArgs, kWorldModelRet, nullptr, true};
const Game::Doc::Function kClearWorldTerrain{"Removes the scene's terrain.", {}, {}, nullptr, true};
const Game::Doc::Field kTraceSegmentArgs[] = {
    Req("startX", "number"), Req("startY", "number"), Req("startZ", "number"),
    Req("endX", "number"),   Req("endY", "number"),   Req("endZ", "number"),
    Opt("camera", "bool", "false",
        "Tests the faces that the world's camera collides with instead: small details count, and faces "
        "made to let the camera through do not."),
};
const Game::Doc::Field kTraceSegmentRets[] = {
    Req("hitX", "number"),
    Req("hitY", "number"),
    Req("hitZ", "number"),
    Req("normalX", "number", "The unit normal of the face, on the side of the start."),
    Req("normalY", "number"),
    Req("normalZ", "number"),
    Req("fraction", "number", "0 at the start, 1 at the end."),
};
const Game::Doc::Function kTraceSegment{
    "Returns the first face of the WMO or of the scene's terrain on the segment from the start to the end, "
    "in scene positions. It tests the faces that the world's line of sight tests: the WMO's walls, floors "
    "and roofs, not small details, and the terrain. Returns nothing when the segment hits no face, when the "
    "scene has no WMO and no terrain, or for parts of the WMO that have not loaded.",
    kTraceSegmentArgs, kTraceSegmentRets, nullptr, true};
const Game::Doc::Field kLiquidArgs[] = {Req("pointX", "number"), Req("pointY", "number"),
                                        Req("pointZ", "number")};
const Game::Doc::Field kLiquidRets[] = {
    Req("liquidType", "number", "0 water, 1 ocean, 2 magma, 3 slime."),
    Opt("surfaceZ", "number", nullptr,
        "The height of the surface above the point. Nil when that part of the WMO is liquid throughout."),
};
const Game::Doc::Function kGetLiquidAtPoint{
    "Returns the liquid of the WMO or of the scene's terrain that a scene point is in, as the world finds "
    "it for a unit that stands there: the WMO's first, then the terrain's. Returns nothing when the point "
    "is above the surface or not in a liquid, or for parts of the WMO that have not loaded.",
    kLiquidArgs, kLiquidRets, nullptr, true};
const Game::Doc::Field kSweepArgs[] = {
    Req("feetX", "number"),
    Req("feetY", "number"),
    Req("feetZ", "number"),
    Req("radius", "number", "Half the width of the body."),
    Req("height", "number"),
    Req("moveX", "number"),
    Req("moveY", "number"),
    Req("moveZ", "number"),
};
const Game::Doc::Field kSweepRets[] = {
    Req("feetX", "number", "Where the feet stop."),
    Req("feetY", "number"),
    Req("feetZ", "number"),
    Opt("normalX", "number", nullptr, "The unit normal of the face the body met. It points toward the side that the face blocks from."),
    Opt("normalY", "number"),
    Opt("normalZ", "number"),
};
const Game::Doc::Function kSweepCollider{
    "Moves a body in a straight line until it touches the WMO, one of its doodads or the scene's terrain, "
    "as the world moves a character into a wall or onto a floor. The body has the shape a character has "
    "in the world: a square box, radius from the feet on each side and height tall, over a downward point "
    "at the feet. A face blocks the body only from its front side. Returns where the feet stop, and the "
    "normal of the face the body met, or only the feet when it met none. Without a WMO or terrain the body "
    "moves the full distance. While a part of the WMO in the way is still loading, the body does not move.",
    kSweepArgs, kSweepRets, nullptr, true};
const Game::Doc::Field kListenerArg[] = {
    Opt("actor", "ModelSceneFrameActor", nullptr, "An actor of this scene. Without it, the camera hears the sounds."),
};
const Game::Doc::Field kListenerRet[] = {Opt("actor", "ModelSceneFrameActor")};
const Game::Doc::Function kSetSoundListenerActor{
    "Sets the actor that hears the scene's sounds, as the world hears them at your character: at the top of "
    "the actor's model, facing the way the actor faces. This applies while the SoundListenerAtCharacter "
    "setting is on. Otherwise, and without an actor, the scene's camera hears them.",
    kListenerArg, {}, nullptr, true};
const Game::Doc::Function kGetSoundListenerActor{
    "Returns the actor that SetSoundListenerActor set, or nothing.", {}, kListenerRet, nullptr, true};
const Game::Doc::Field kWorldModelSoundsArg[] = {Req("enabled", "bool")};
const Game::Doc::Function kSetPlayWorldModelSounds{
    "Sets whether the WMO's props play their sounds, as they do in the world, heard from the scene's "
    "camera: a torch is heard near it and not far away. Only in the world, not at the glue screen. Off by "
    "default.",
    kWorldModelSoundsArg, {}, nullptr, true};
const Game::Doc::Function kGetPlayWorldModelSounds{
    "Returns whether the WMO's props play their sounds.", {}, kWorldModelSoundsArg, nullptr, true};
const Game::Doc::Field kTimeOfDayArg[] = {
    Req("timeOfDay", "number", "A share of the day: 0 midnight, 0.25 six in the morning, 0.5 noon."),
};
const Game::Doc::Field kTimeOfDayRet[] = {
    Opt("timeOfDay", "number", nullptr, "Nil while the scene shows the world's time."),
};
const Game::Doc::Function kSetWorldTimeOfDay{
    "Lights the scene's terrain or map WMO as the world is lit at a time of day, by the map's light where the "
    "camera is: the sun lights the actors and the ground from where it is at that time, with that time's "
    "colours, the fog is the map's, and the sky, the sun, the moons and the stars show that time. This light "
    "takes the place of the scene's light and fog while the scene draws. Without it the sky shows the world's "
    "time and the scene's light and fog light everything.",
    kTimeOfDayArg, {}, nullptr, true};
const Game::Doc::Function kClearWorldTimeOfDay{
    "Gives the scene back its own light and fog, and the world's time for its sky.", {}, {}, nullptr, true};
const Game::Doc::Function kGetWorldTimeOfDay{"Returns the time of day SetWorldTimeOfDay gave.", {}, kTimeOfDayRet,
                                             nullptr, true};
const Game::Doc::Field kStrengthArg[] = {Req("strength", "number", "0 to 1.")};
const Game::Doc::Function kSetDesaturation{
    "Sets how much color is removed from each actor the scene has now. Actors created later are not changed.",
    kStrengthArg, {}};
const Game::Doc::Field kOverlappedArg[] = {Req("allowOverlappedModels", "bool")};
const Game::Doc::Field kOverlappedRet[] = {Req("allowOverlappedModels", "bool")};
const Game::Doc::Function kSetAllowOverlappedModels{
    "Sets whether the scene clears the depth under it before it draws. Without it, a scene that overlaps "
    "another scene drawn earlier in the same frame can be hidden behind that scene's models.",
    kOverlappedArg, {}};
const Game::Doc::Function kGetAllowOverlappedModels{"Returns the flag that SetAllowOverlappedModels stores.",
                                                    {}, kOverlappedRet};
const Game::Doc::Field kProjectArgs[] = {Req("pointX", "number"), Req("pointY", "number"),
                                         Req("pointZ", "number")};
const Game::Doc::Field kProjectRets[] = {
    Req("point2DX", "number", "From the scene's left edge, in the frame's units times its effective scale."),
    Req("point2DY", "number", "From the scene's bottom edge, in the same units."),
    Req("depth", "number", "1 at the near clip, 0 at the far clip, by distance along the camera's forward axis."),
};
const Game::Doc::Function kProject3DPointTo2D{
    "Returns where a scene point appears on the scene. Returns nothing before the scene has a size, "
    "or for a point in the camera's plane.",
    kProjectArgs, kProjectRets};
const Game::Doc::Function kGetActorAtCursor{
    "Returns the shown actor that the scene draws nearest under the cursor. The test uses the "
    "triangles of the model, its held items and its effects. Returns nothing when no actor is "
    "under the cursor.",
    {}, kActorRet, nullptr, true};
const Game::Doc::Field kCursorRayRets[] = {
    Req("nearX", "number"), Req("nearY", "number"), Req("nearZ", "number"),
    Req("farX", "number"),  Req("farY", "number"),  Req("farZ", "number"),
};
const Game::Doc::Function kGetCursorRay{
    "Returns the line through the cursor, from the point under it on the near clip to the point "
    "under it on the far clip, in scene coordinates. Returns nothing when the cursor is not over the "
    "picture of the scene, or the scene is hidden.",
    {}, kCursorRayRets, nullptr, true};
const Game::Doc::Field kRadiusArgs[] = {
    Req("pointX", "number"), Req("pointY", "number"), Req("pointZ", "number"),
    Req("radius", "number", "In yards."),
};
const Game::Doc::Field kRadiusRets[] = {
    Req("actors", "table", "The actors, nearest first. Actors at the same distance stay in creation order."),
};
const Game::Doc::Function kGetActorsInRadius{
    "Returns the shown actors with a model whose position is not more than the radius from the point, "
    "in scene coordinates. The list is empty when no actor is in the radius.",
    kRadiusArgs, kRadiusRets, nullptr, true};
const Game::Doc::Field kMarkerPoint[] = {Req("pointX", "number"), Req("pointY", "number"),
                                         Req("pointZ", "number")};
const Game::Doc::Function kSetMoveMarker{
    "Shows the world's click-to-move marker at a point, in scene coordinates: a pulsing ring on the "
    "ground. One marker per scene; a new point moves it.",
    kMarkerPoint, {}, nullptr, true};
const Game::Doc::Function kClearMoveMarker{"Hides the move marker.", {}, {}, nullptr, true};
const Game::Doc::Function kGetMoveMarker{
    "Returns the point of the move marker. Returns nothing while the marker is hidden.", {}, kMarkerPoint,
    nullptr, true};
const Game::Doc::Field kInsetArgs[] = {Req("left", "number"), Req("right", "number"), Req("top", "number"),
                                       Req("bottom", "number")};
const Game::Doc::Field kInsetRets[] = {Req("left", "number"), Req("right", "number"), Req("top", "number"),
                                       Req("bottom", "number")};
const Game::Doc::Field kTranslationArgs[] = {Req("translationX", "number"), Req("translationY", "number")};
const Game::Doc::Field kTranslationRets[] = {Req("translationX", "number"), Req("translationY", "number")};
const Game::Doc::Function kSetViewInsets{
    "Frames the camera's picture in the scene less these insets, in the frame's units. Negative insets "
    "reach past the frame. Drawing still stops at the frame's edges. Project3DPointTo2D ignores them.",
    kInsetArgs, {}};
const Game::Doc::Function kGetViewInsets{"Returns the view insets.", {}, kInsetRets};
const Game::Doc::Function kSetViewTranslation{
    "Moves the camera's picture by this offset, in the frame's units, with y up. Drawing still stops at the "
    "frame's edges. Project3DPointTo2D ignores it.",
    kTranslationArgs, {}};
const Game::Doc::Function kGetViewTranslation{"Returns the view translation.", {}, kTranslationRets};
const Game::Doc::Function kSetLightType{"Makes the light directional or a point light.",
                                        kLightTypeArg, {}};
const Game::Doc::Function kGetLightType{"Returns the light's type.", {}, kLightTypeRet};
const Game::Doc::Function kSetLightVisible{"Turns the light on or off.", kVisibleArg, {}};
const Game::Doc::Function kIsLightVisible{"Returns whether the light is on.", {}, kVisibleRet};
const Game::Doc::Function kSetLightDirection{"Sets the direction of a directional light.",
                                             kDirectionArgs, {}};
const Game::Doc::Function kGetLightDirection{"Returns the light's direction.", {}, kDirectionRets};
const Game::Doc::Function kSetLightPosition{"Sets the position of a point light.", kPositionArgs,
                                            {}};
const Game::Doc::Function kGetLightPosition{"Returns the light's position.", {}, kPositionRets};
const Game::Doc::Function kSetLightAmbientColor{"Sets the light's ambient colour.", kColorArgs, {}};
const Game::Doc::Function kGetLightAmbientColor{"Returns the light's ambient colour.", {},
                                                kColorRets};
const Game::Doc::Function kSetLightDiffuseColor{"Sets the light's diffuse colour.", kColorArgs, {}};
const Game::Doc::Function kGetLightDiffuseColor{"Returns the light's diffuse colour.", {},
                                                kColorRets};
const Game::Doc::Function kSetFogColor{"Sets the fog colour. The fog starts white.", kColorArgs, {}};
const Game::Doc::Function kGetFogColor{"Returns the fog colour.", {}, kColorRets};
const Game::Doc::Function kSetFogNear{"Sets the distance where the fog starts.", kNearFogArg, {}};
const Game::Doc::Function kGetFogNear{"Returns the distance where the fog starts.", {},
                                      kNearFogRet};
const Game::Doc::Function kSetFogFar{"Sets the distance where the fog is full.", kFarFogArg, {}};
const Game::Doc::Function kGetFogFar{"Returns the distance where the fog is full.", {},
                                     kFarFogRet};
const Game::Doc::Function kClearFog{
    "Moves the fog's start and end back past anything the scene draws. The colour stays.", {}, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetDrawLayer", &kSetDrawLayer},
    {"GetDrawLayer", &kGetDrawLayer},
    {"SetPaused", &kSetPaused},
    {"GetSceneTime", &kGetSceneTime},
    {"After", &kAfter},
    {"NewTimer", &kNewTimer},
    {"NewTicker", &kNewTicker},
    {"SetAllowOverlappedModels", &kSetAllowOverlappedModels},
    {"SetDesaturation", &kSetDesaturation},
    {"GetAllowOverlappedModels", &kGetAllowOverlappedModels},
    {"SetGroundTerrain", &kSetGroundTerrain},
    {"GetGroundTerrain", &kGetGroundTerrain},
    {"SetWorldModel", &kSetWorldModel},
    {"SetWorldModelByMapID", &kSetWorldModelByMapID},
    {"ClearWorldModel", &kClearWorldModel},
    {"SetWorldTerrain", &kSetWorldTerrain},
    {"ClearWorldTerrain", &kClearWorldTerrain},
    {"TraceSegment", &kTraceSegment},
    {"GetLiquidAtPoint", &kGetLiquidAtPoint},
    {"SweepCollider", &kSweepCollider},
    {"SetSoundListenerActor", &kSetSoundListenerActor},
    {"GetSoundListenerActor", &kGetSoundListenerActor},
    {"SetPlayWorldModelSounds", &kSetPlayWorldModelSounds},
    {"GetPlayWorldModelSounds", &kGetPlayWorldModelSounds},
    {"SetWorldTimeOfDay", &kSetWorldTimeOfDay},
    {"ClearWorldTimeOfDay", &kClearWorldTimeOfDay},
    {"GetWorldTimeOfDay", &kGetWorldTimeOfDay},
    {"Project3DPointTo2D", &kProject3DPointTo2D},
    {"GetActorAtCursor", &kGetActorAtCursor},
    {"GetCursorRay", &kGetCursorRay},
    {"GetActorsInRadius", &kGetActorsInRadius},
    {"SetMoveMarker", &kSetMoveMarker},
    {"ClearMoveMarker", &kClearMoveMarker},
    {"GetMoveMarker", &kGetMoveMarker},
    {"SetViewInsets", &kSetViewInsets},
    {"GetViewInsets", &kGetViewInsets},
    {"SetViewTranslation", &kSetViewTranslation},
    {"GetViewTranslation", &kGetViewTranslation},
    {"SetLightType", &kSetLightType},
    {"GetLightType", &kGetLightType},
    {"SetLightVisible", &kSetLightVisible},
    {"IsLightVisible", &kIsLightVisible},
    {"SetLightDirection", &kSetLightDirection},
    {"GetLightDirection", &kGetLightDirection},
    {"SetLightPosition", &kSetLightPosition},
    {"GetLightPosition", &kGetLightPosition},
    {"SetLightAmbientColor", &kSetLightAmbientColor},
    {"GetLightAmbientColor", &kGetLightAmbientColor},
    {"SetLightDiffuseColor", &kSetLightDiffuseColor},
    {"GetLightDiffuseColor", &kGetLightDiffuseColor},
    {"SetFogColor", &kSetFogColor},
    {"GetFogColor", &kGetFogColor},
    {"SetFogNear", &kSetFogNear},
    {"GetFogNear", &kGetFogNear},
    {"SetFogFar", &kSetFogFar},
    {"GetFogFar", &kGetFogFar},
    {"ClearFog", &kClearFog},
    {"CreateActor", &kCreateActor},
    {"GetNumActors", &kGetNumActors},
    {"GetActorAtIndex", &kGetActorAtIndex},
    {"TakeActor", &kTakeActor},
    {"SetCameraPosition", &kSetCameraPosition},
    {"GetCameraPosition", &kGetCameraPosition},
    {"SetCameraOrientationByYawPitchRoll", &kSetCameraOrientationByYawPitchRoll},
    {"SetCameraOrientationByAxisVectors", &kSetCameraOrientationByAxisVectors},
    {"GetCameraForward", &kGetCameraForward},
    {"GetCameraRight", &kGetCameraRight},
    {"GetCameraUp", &kGetCameraUp},
    {"SetCameraFieldOfView", &kSetCameraFieldOfView},
    {"GetCameraFieldOfView", &kGetCameraFieldOfView},
    {"SetCameraNearClip", &kSetCameraNearClip},
    {"GetCameraNearClip", &kGetCameraNearClip},
    {"SetCameraFarClip", &kSetCameraFarClip},
    {"GetCameraFarClip", &kGetCameraFarClip},
};

void RegisterLuaFunctions() {
    Frame::Subclass::RegisterFactory(g_class.name, &CreateModelScene);
    Api::Documentation::NameWidgetSystem(g_class.hash.Registry(), "FrameAPIModelSceneFrame");
    Game::Lua::RegisterFrameMethods(g_class.hash.Registry(), g_methods, g_methodDocs);
    RegisterActorClass();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool NumberArgs(void *L, int first, int count, const char *usage) {
    for (int i = 0; i < count; ++i) {
        if (!Game::Lua::IsNumber(L, first + i))
            return Game::Lua::MethodArg::Fail(L, first + i, usage);
    }
    return true;
}

float FloatArg(void *L, int index) { return static_cast<float>(Game::Lua::ToNumber(L, index)); }

int GroundTerrain(void *sceneFrame) { return F(sceneFrame).groundTerrain; }

bool IsSceneFrame(void *L, int idx) {
    return Game::Lua::Type(L, idx) == Game::Lua::TYPE_TABLE &&
           Game::Lua::ResolveTypedObject(L, idx, g_class.TypeIdVar(), false) != nullptr;
}

bool IsActor(void *L, int idx) { return Animation::Resolve(L, idx, Actor::s_class, false) != nullptr; }

World::Backdrop *WorldOf(void *sceneFrame) { return F(sceneFrame).world; }

Terrain::Land *TerrainOf(void *sceneFrame) { return F(sceneFrame).terrain; }

Water::Effects *&WaterOf(void *sceneFrame) { return F(sceneFrame).water; }

bool QueryGroundFacets(void *sceneFrame, const float (&box)[6], std::vector<Contact::Facet> &out) {
    const Fields &f = F(sceneFrame);
    out.clear();
    if (f.world == nullptr && f.terrain == nullptr)
        return false;
    if (f.world != nullptr && !World::QueryFacets(f.world, box, out))
        return false;
    return f.terrain == nullptr || Terrain::QueryFacets(*f.terrain, box, out);
}

bool QueryGroundLiquidFacets(void *sceneFrame, const float (&box)[6], std::vector<Contact::Facet> &out) {
    const Fields &f = F(sceneFrame);
    out.clear();
    if (f.world == nullptr && f.terrain == nullptr)
        return false;
    if (f.world != nullptr && !World::QueryLiquidFacets(f.world, box, out))
        return false;
    return f.terrain == nullptr || Terrain::QueryLiquidFacets(*f.terrain, box, out);
}

bool LiquidAtPoint(void *sceneFrame, const float (&point)[3], uint32_t &type, float &surface) {
    const Fields &f = F(sceneFrame);
    if (f.world != nullptr && World::LiquidAt(*f.world, point, type, surface))
        return true;
    return f.terrain != nullptr && Terrain::LiquidAt(*f.terrain, point, type, surface);
}

uint32_t OutdoorCameraLiquid(void *sceneFrame) {
    const View view = CameraView(sceneFrame);
    const float probe[3] = {view.eye[0], view.eye[1], view.eye[2] - F(sceneFrame).probeDrop};
    uint32_t type = 0;
    float surface = 0.0f;
    return LiquidAtPoint(sceneFrame, probe, type, surface) ? type : static_cast<uint32_t>(Offsets::LIQUID_DRY);
}

float CameraProbeDrop(void *sceneFrame) { return F(sceneFrame).probeDrop; }

uint32_t CameraLiquid(void *sceneFrame) { return F(sceneFrame).cameraLiquid; }

bool TraceGround(void *sceneFrame, const float (&start)[3], const float (&end)[3], bool camera, float &fraction,
                 float (&normal)[3]) {
    const Fields &f = F(sceneFrame);
    fraction = 1.0f;
    bool hit = false;
    float wmoFraction = 1.0f;
    if (f.world != nullptr && World::Trace(*f.world, start, end, camera, wmoFraction, normal)) {
        fraction = wmoFraction;
        hit = true;
    }
    if (f.terrain != nullptr && Terrain::Trace(*f.terrain, start, end, fraction, normal))
        hit = true;
    return hit;
}

bool HasGround(void *sceneFrame) { return F(sceneFrame).world != nullptr || F(sceneFrame).terrain != nullptr; }

Actor *SoundListener(void *sceneFrame) { return F(sceneFrame).listener; }

const float *LightAmbient(void *sceneFrame) { return AmbientOf(sceneFrame); }

float GroundReticleRadius(const Actor &actor) {
    if (actor.groundReticleRadius != 0.0f)
        return actor.groundReticleRadius;
    const uint32_t bits = Offsets::RETICLE_DEFAULT_RADIUS;
    float radius;
    std::memcpy(&radius, &bits, sizeof radius);
    return radius;
}

bool HasFog(void *sceneFrame) { return F(sceneFrame).fogNear < F(sceneFrame).farClip; }

uint32_t ClockMs(void *sceneFrame) {
    return Game::Read<uint32_t>(Model::Pane::Scene(sceneFrame), Offsets::OFF_M2SCENE_TIME);
}

View CameraView(void *sceneFrame) {
    const Fields &f = F(sceneFrame);
    return {{f.position.x, f.position.y, f.position.z},
            {f.forward.x, f.forward.y, f.forward.z},
            {f.up.x, f.up.y, f.up.z},
            DrawFarClip(sceneFrame)};
}

} // namespace Model::Scene
