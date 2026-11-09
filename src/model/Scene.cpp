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
// Fog is the Model frame's own (+0x3A4..+0x3B0), stored the way Model's
// SetFogColor / SetFogNear / SetFogFar / ClearFog store it (0x0076EE60..
// 0x0076F5C5): the colour arms it, ClearFog disarms it only. It starts with
// retail's values (near 1e7, far 1e8, white), which fog nothing. SetPaused
// stops the scene clock: the frame's update runs without Model's
// M2SceneAdvance, so every actor holds its pose.
//
// XML: `drawLayer`, `fogNear` and `fogFar` (clamped at 0, as Model's loader
// clamps them) and <FogColor> (FUN_XML_READ_COLOR, which arms the fog), after
// the frame's own loader, and <ViewInsets left right top bottom>. Retail's
// <Actors> child is not read; no retail UI uses it. Not ported, as no UI this
// DLL targets uses them and their exact effect is unprobed: desaturation and
// TakeActor.
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
// SetAllowOverlappedModels only stores its flag (retail's default false,
// probed): 1.12 draws a model frame whatever overlaps it, so there is nothing
// for the flag to allow. Project3DPointTo2D is under "projection" below.
//
// ClassicAPI extensions: GetActorAtCursor, and the scripts OnActorEnter /
// OnActorLeave (self, actor), which run from the update walk when the actor
// under the cursor changes while the scene is the mouse focus. Both use the
// world's own object pick, under "picking" below. SetWorldModel /
// SetWorldModelByMapID / ClearWorldModel: a WMO drawn by the world's own map
// object draw after the animate pass, with its doodads as models of the
// scene; Set/GetPlayWorldModelSounds: the doodads' sounds
// (model/SceneWorld.cpp).

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "api/Documentation.h"
#include "frame/ExtraScripts.h"
#include "frame/LayerChange.h"
#include "frame/Layout.h"
#include "frame/ScriptArgs.h"
#include "frame/Subclass.h"
#include "gx/Device.h"
#include "model/Camera.h"
#include "model/M2.h"
#include "xml/FrameLoad.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace Model::Scene {

namespace {

using Frame::Layout::OwnerOf;
using Model::Camera::Vec3;

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
    uint32_t paused;
    uint32_t allowOverlappedModels;
    // In the frame's own units: left, right, top, bottom; then x, y (y up).
    float viewInsets[4];
    float viewTranslation[2];
    int32_t groundTerrain; // SetGroundTerrain: a TerrainType id, or -1
    Actor *hovered; // the actor OnActorEnter last ran for, or null
    World::Backdrop *world; // SetWorldModel, owned, or null
    bool playWorldModelSounds; // SetPlayWorldModelSounds
};

constexpr size_t kObjectSize = Offsets::MODEL_FRAME_SIZE + sizeof(Fields);

Fields &F(void *frame) { return *Game::Ptr<Fields>(frame, Offsets::MODEL_FRAME_SIZE); }

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
    SetScalar(camera, Offsets::CAMERA_PROP_NEAR_CLIP, f.nearClip);
    SetScalar(camera, Offsets::CAMERA_PROP_FAR_CLIP, f.farClip);
}

// Retail's axes from yaw, pitch and roll (Vector3D_CalculateNormalFromYawPitch:
// forward = (cos p cos y, cos p sin y, -sin p), positive pitch looking down;
// the retail probe: right = (0, 1, 0) and up = (0, 0, 1) at rest, and roll
// turns both about forward).
void SetOrientation(Fields &f, float yaw, float pitch, float roll) {
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const Vec3 right0{-sy, cy, 0.0f};
    const Vec3 up0{sp * cy, sp * sy, cp};
    f.forward = {cp * cy, cp * sy, -sp};
    f.right = {cr * right0.x + sr * up0.x, cr * right0.y + sr * up0.y, cr * right0.z + sr * up0.z};
    f.up = {cr * up0.x - sr * right0.x, cr * up0.y - sr * right0.y, cr * up0.z - sr * right0.z};
}

void InstallCamera(void *frame) {
    using CameraCreate_t = void *(__cdecl *)();
    using Install_t = void(__thiscall *)(void *frame, void *camera);
    using Release_t = void(__fastcall *)(void *handle);
    void *camera = reinterpret_cast<CameraCreate_t>(Offsets::FUN_CAMERA_CREATE)();
    if (camera == nullptr)
        return;
    reinterpret_cast<Install_t>(Offsets::FUN_SIMPLEMODEL_INSTALL_CAMERA)(frame, camera);
    reinterpret_cast<Release_t>(Offsets::FUN_HANDLE_RELEASE)(camera);
    ApplyCamera(frame);
}

// ---- the light and fog ---------------------------------------------------------------

using LightInt_t = void(__thiscall *)(void *light, int value);
using LightVector_t = void(__thiscall *)(void *light, const float *v);

void *LightOf(void *frame) { return Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT); }
float *AmbientOf(void *frame) { return Game::Ptr<float>(LightOf(frame), Offsets::OFF_M2LIGHT_AMBIENT); }
float *DiffuseOf(void *frame) { return Game::Ptr<float>(LightOf(frame), Offsets::OFF_M2LIGHT_DIFFUSE); }

void SetLightType(void *frame, uint32_t type) {
    F(frame).lightType = type;
    reinterpret_cast<LightInt_t>(Offsets::FUN_M2LIGHT_SET_TYPE)(LightOf(frame), static_cast<int>(type));
}

void SetLightVisible(void *frame, bool visible) {
    F(frame).lightVisible = visible ? 1 : 0;
    reinterpret_cast<LightInt_t>(Offsets::FUN_M2LIGHT_SET_ENABLED)(LightOf(frame), visible ? 1 : 0);
}

void SetLightDirection(void *frame, const Vec3 &v) {
    F(frame).lightDirection = v;
    reinterpret_cast<LightVector_t>(Offsets::FUN_M2LIGHT_SET_DIRECTION)(LightOf(frame), &v.x);
}

void SetLightPosition(void *frame, const Vec3 &v) {
    F(frame).lightPosition = v;
    reinterpret_cast<LightVector_t>(Offsets::FUN_M2LIGHT_SET_POSITION)(LightOf(frame), &v.x);
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

// Model's SetFogColor byte: clamped to [0, 1] (NaN to 1), trunc(c * 255 + 0.5).
uint32_t FogByte(float c) {
    if (!(c < 1.0f))
        c = 1.0f;
    else if (!(c > 0.0f))
        c = 0.0f;
    return static_cast<uint32_t>(c * 255.0f + 0.5f);
}

uint32_t &FogFlags(void *frame) { return Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FLAGS); }

void SetFogColor(void *frame, const Vec3 &c) {
    FogFlags(frame) |= Offsets::SIMPLEMODEL_FOG_ARMED;
    Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR) =
        0xFF000000u | (FogByte(c.x) << 16) | (FogByte(c.y) << 8) | FogByte(c.z);
}

void InitFog(void *frame) {
    Game::Ref<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR) = 0xFFFFFFFFu;
    Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR) = 1.0e7f;
    Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) = 1.0e8f;
}

// ---- the paint (SimpleModelDraw) -----------------------------------------------

void PlaceActors(void *frame) {
    for (Actor *actor : *F(frame).actors)
        actor->Place();
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
    const float scale = Game::Read<float>(frame, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    auto internal = [scale](float length) { return Game::PixelToInternal(length * scale); };
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
// is laid the same way on the WMO's faces (World::DrawDecal). A scene without
// one has no ground, so the turned box itself is drawn, flat on the plane of
// the base translation (the model's origin, its feet), culling off as for the
// engine's other single quads (the lightning bolt, 0x007AFCB0), with no
// height fade and no depth function (the quad is its own geometry). A model
// hung on another (a seated rider) casts none: its base transform is relative
// to its parent, and the world's entity list holds the mount alone.
//
// The shadows draw after the WMO and the animate pass and before the models,
// without depth writes: each model then draws over the shadows, its own
// included.

using GxRsSet_t = void(__fastcall *)(int selector, int value);
using GxRsSetPtr_t = void(__fastcall *)(int selector, const void *value);
using GxRsSetWord_t = void(__fastcall *)(int selector, int, uint32_t value);
using GxState_t = void(__fastcall *)();
using GetRenderable_t = void *(__fastcall *)(void *texture, int force, void *);
using PrimStreams_t = void(__fastcall *)(int count, const void *pos, int posStride, const void *normal,
                                         int normalStride, const void *colors, int colorStride,
                                         int unused8, int unused9, const void *uv0, int uv0Stride,
                                         const void *uv1, int uv1Stride);
using PrimIndexed_t = void(__fastcall *)(int primType, int indexCount, const uint16_t *indices);

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
    const float epsilon = Game::Read<float>(Offsets::VAR_HAS_RANGE_EPSILON);
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
    reinterpret_cast<PrimStreams_t>(Offsets::FUN_GX_PRIM_STREAMS)(4, pos, 12, nullptr, 0, colors, 4, 0, 0,
                                                                 uv, 8, nullptr, 0);
    reinterpret_cast<PrimIndexed_t>(Offsets::FUN_GX_PRIM_INDEXED)(Offsets::GX_PRIM_TRIANGLE_STRIP, 4,
                                                                  kStrip);
}

// Draws one actor's shadow; the blob states are set.
void DrawShadow(Actor &actor, World::Backdrop *world, const float (&eye)[3]) {
    void *model = actor.model;
    if (!actor.shadowShown || !actor.shown || model == nullptr || M2::Parent(model) != nullptr ||
        !M2::IsReady(model, 0, 0))
        return;
    Blob blob;
    if (!MakeBlob(model, blob))
        return;
    if (world == nullptr) {
        DrawQuad(blob, eye);
        return;
    }
    const uint32_t fadeBits = Offsets::DECAL_SHADOW_FADE;
    float fade;
    std::memcpy(&fade, &fadeBits, sizeof fade);
    World::DrawDecal(*world, blob.box, blob.color, blob.matrix, fade, Offsets::SHADOW_DECAL_MASK, eye);
}

void DrawShadows(void *frame, const float (&eye)[3]) {
    void *texture = Game::Read<void *>(Offsets::VAR_SHADOW_BLOB_TEXTURE);
    if (Game::Read<int>(Offsets::VAR_SHADOW_LOD) != 1 || texture == nullptr)
        return;
    const std::vector<Actor *> &actors = *F(frame).actors;
    bool any = false;
    for (const Actor *actor : actors)
        any = any || actor->shadowShown;
    if (!any)
        return;
    World::Backdrop *world = CameraOf(frame) != nullptr ? F(frame).world : nullptr;
    auto rs = reinterpret_cast<GxRsSet_t>(Offsets::FUN_GX_RS_SET);
    reinterpret_cast<GxState_t>(Offsets::FUN_GX_STATE_PUSH)();
    rs(Offsets::GXRS_BLEND, Offsets::GXRS_BLEND_MODULATE);
    rs(Offsets::GXRS_LIGHTING, 0);
    rs(Offsets::GXRS_FOG, 0);
    rs(Offsets::GXRS_DEPTH_WRITE, 0);
    reinterpret_cast<GxRsSetPtr_t>(Offsets::FUN_GX_RS_SET_PTR)(
        Offsets::GXRS_TEXTURE0,
        reinterpret_cast<GetRenderable_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE)(texture, 1, nullptr));
    reinterpret_cast<GxRsSetWord_t>(Offsets::FUN_GX_RS_SET_WORD)(Offsets::GXRS_FOG_COLOR, 0, 0xFFFFFFFFu);
    rs(Offsets::GXRS_COMBINER0, Offsets::GXRS_COMBINE_BLEND_DIFFUSE_ALPHA);
    if (world == nullptr)
        rs(Offsets::GXRS_CULL, 0);
    else if (Game::Read<float>(Offsets::VAR_SHADOW_BIAS) == 0.0f)
        rs(Offsets::GXRS_DEPTH_FUNC, Offsets::GXRS_DEPTH_FUNC_DECAL);
    for (Actor *actor : actors)
        DrawShadow(*actor, world, eye);
    reinterpret_cast<GxState_t>(Offsets::FUN_GX_STATE_POP)();
}

void __fastcall Paint(void *frame) {
    using SetupOrtho_t = void(__fastcall *)(const float *rect, const float *point, int unused);
    using DdcToNdc_t = void(__fastcall *)(float *x, float *y, float inX, float inY);
    using Clear_t = void(__fastcall *)(uint32_t mask);
    using Draw_t = void(__thiscall *)(void *scene, uint32_t pass);

    // {bottom, left, top, right}
    float rect[4] = {};
    if (!Frame::Layout::GetRect(frame, rect))
        return;
    const Gx::SavedTransforms saved;
    const Gx::Viewport &vp = saved.SavedViewport();
    float eye[3] = {0.0f, 0.0f, 0.0f};
    if (void *camera = CameraOf(frame)) {
        const Vec3 e = Model::Camera::GetVector(camera, Offsets::CAMERA_PROP_EYE);
        eye[0] = e.x;
        eye[1] = e.y;
        eye[2] = e.z;
        CameraTransforms(frame, camera, rect);
    } else {
        const float point[2] = {rect[1], rect[0]};
        reinterpret_cast<SetupOrtho_t>(Offsets::FUN_SETUP_ORTHO_TRANSFORMS)(rect, point, 0);
    }
    auto toNdc = reinterpret_cast<DdcToNdc_t>(Offsets::FUN_COORD_DDC_TO_NDC);
    toNdc(&rect[1], &rect[0], rect[1], rect[0]);
    toNdc(&rect[3], &rect[2], rect[3], rect[2]);
    for (float &edge : rect)
        edge = ClampUnit(edge);
    const float epsilon = Game::Read<float>(Offsets::VAR_HAS_RANGE_EPSILON); // 2^-22
    if (!(std::fabs(rect[1] - rect[3]) < epsilon) && !(std::fabs(rect[0] - rect[2]) < epsilon)) {
        Gx::SetViewport({rect[1] * vp.maxX, rect[3] * vp.maxX, rect[0] * vp.maxY, rect[2] * vp.maxY,
                         0.0f, 1.0f});
        reinterpret_cast<Clear_t>(Offsets::FUN_GX_CLEAR)(2);
        PlaceActors(frame);
        void *scene = Model::Pane::Scene(frame);
        M2::AnimateScene(scene, eye);
        World::Backdrop *world = CameraOf(frame) != nullptr ? F(frame).world : nullptr;
        if (world != nullptr)
            World::Draw(*world, frame, eye);
        DrawShadows(frame, eye);
        auto draw = reinterpret_cast<Draw_t>(Offsets::FUN_M2SCENE_DRAW);
        draw(scene, 0);
        // The world's frame draws its liquids before the translucent models
        // over the water and, with the camera in liquid, after them
        // (0x004836D6).
        const bool wet = world != nullptr && World::CameraWet(*world);
        if (wet)
            draw(scene, 1);
        if (world != nullptr)
            World::DrawLiquids(*world, frame, eye);
        if (!wet)
            draw(scene, 1);
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
// near 0.2/1, width and field of view, but k = 0.4607 at scale 1 and 1.8427
// at SetScale(2) — about 0.911 * effectiveScale^2, a factor 1.12 has no
// source for. Depth here is the same form without it (1 at the near clip,
// 0 at the far); retail's callers all discard it.
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
    out[2] = (f.farClip - c[3]) / (f.farClip - f.nearClip);
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

// The cursor in the clip space of the paint's viewport, the frame's rect
// {bottom, left, top, right} in NDC clamped to the screen; false when the
// cursor is outside it or the paint draws nothing.
bool CursorClip(const float (&rect)[4], float &x, float &y) {
    using DdcToNdc_t = void(__fastcall *)(float *x, float *y, float inX, float inY);
    const uint8_t *ctx = Game::Read<const uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    if (ctx == nullptr)
        return false;
    const float *mouse = Game::Ptr<float>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    float vp[4] = {rect[0], rect[1], rect[2], rect[3]};
    auto toNdc = reinterpret_cast<DdcToNdc_t>(Offsets::FUN_COORD_DDC_TO_NDC);
    toNdc(&vp[1], &vp[0], vp[1], vp[0]);
    toNdc(&vp[3], &vp[2], vp[3], vp[2]);
    for (float &edge : vp)
        edge = ClampUnit(edge);
    const float epsilon = Game::Read<float>(Offsets::VAR_HAS_RANGE_EPSILON);
    if (std::fabs(vp[1] - vp[3]) < epsilon || std::fabs(vp[0] - vp[2]) < epsilon)
        return false;
    if (!(mouse[0] >= vp[1] && mouse[0] <= vp[3] && mouse[1] >= vp[0] && mouse[1] <= vp[2]))
        return false;
    x = 2.0f * (mouse[0] - vp[1]) / (vp[3] - vp[1]) - 1.0f;
    y = 2.0f * (mouse[1] - vp[0]) / (vp[2] - vp[0]) - 1.0f;
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
    if (!CursorClip(rect, x, y))
        return nullptr;
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

// ---- the overrides -----------------------------------------------------------------

using SetFrameAlpha_t = void(__fastcall *)(void *self, void *edx, uint8_t alpha);
using OnFrameUpdate_t = void(__fastcall *)(void *self, void *edx, float elapsedSec);
using EmitLayer_t = void(__fastcall *)(void *self, void *edx, void *batch, int layer);
using Hide_t = int(__fastcall *)(void *self, void *edx);
using Callback_t = void(__fastcall *)(void *arg);
using AddCallback_t = void(__fastcall *)(void *batch, void *edx, Callback_t fn, void *arg);

// The actors destroyed, before Model's deleting destructor (which frees the
// camera and the scene).
void Destroy(void *frame) {
    Fields &f = F(frame);
    for (Actor *actor : *f.actors)
        delete actor;
    delete f.actors;
    f.actors = nullptr;
    World::Free(f.world);
    f.world = nullptr;
}

Frame::Subclass::Class<Offsets::VTBL_SIMPLEMODEL_SLOTS, Offsets::VTBL_SIMPLEMODEL_LAYOUT_SLOTS>
    g_class{"ModelScene",
            Offsets::VTBL_SIMPLEMODEL,
            Offsets::VTBL_SIMPLEMODEL_LAYOUT,
            Offsets::FUN_SIMPLEMODEL_CONSTRUCTOR,
            kObjectSize,
            &Destroy};

using Frame::Subclass::FrameBase;

// OnActorEnter / OnActorLeave (self, actor): the actor under the cursor
// changed while the scene is the mouse focus, from the update walk.
const Frame::ExtraScripts::Script g_onActorEnter{"onactorenter", g_class.TypeIdVar()};
const Frame::ExtraScripts::Script g_onActorLeave{"onactorleave", g_class.TypeIdVar()};

bool HasScript(const Frame::ExtraScripts::Script &script, void *frame) {
    const uint32_t *slot = script.Slot(frame);
    return slot != nullptr && slot[0] != 0;
}

void RunActorScript(const Frame::ExtraScripts::Script &script, void *frame, Actor *actor) {
    const uint32_t *slot = script.Slot(frame);
    void *L = slot != nullptr && slot[0] != 0 ? Game::Lua::State() : nullptr;
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Animation::Push(L, actor);
    Frame::ScriptArgs::RunSlotWithValues(L, slot, frame, top + 1, 1);
    Game::Lua::SetTop(L, top);
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
    if (World::Backdrop *world = F(self).world) {
        World::Update(*world, self, F(self).playWorldModelSounds, elapsedSec);
        // A seated rider or an attached actor takes its parent's light.
        for (Actor *actor : *F(self).actors)
            if (actor->model != nullptr && M2::Parent(actor->model) == nullptr && M2::IsLoaded(actor->model, 0, 0))
                World::Settle(*world, *actor->ambience, self, actor->model, elapsedSec);
    }
    if (F(self).paused)
        FrameBase<OnFrameUpdate_t>(Offsets::VTBL_FRAME_ON_FRAME_UPDATE)(self, nullptr, elapsedSec);
    else
        g_class.Base<OnFrameUpdate_t>(Offsets::VTBL_FRAME_ON_FRAME_UPDATE)(self, nullptr, elapsedSec);
    // By index: an actor's script may create another.
    std::vector<Actor *> &actors = *F(self).actors;
    for (size_t i = 0; i < actors.size(); ++i)
        actors[i]->Update(elapsedSec);
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
    for (Actor *actor : *F(self).actors)
        Casts::Silence(*actor);
    return g_class.Base<Hide_t>(Offsets::VTBL_FRAME_HIDE)(self, nullptr);
}

using LoadXML_t = uintptr_t(__fastcall *)(void *layout, void *edx, const void *node, void *status);

// The frame's own loader, then ModelSceneType's attributes and <FogColor>.
uintptr_t __fastcall LoadXML(void *layout, void *, const void *node, void *status) {
    using LayerFromName_t = int(__fastcall *)(const char *name, int *layer);
    using ReadColor_t = int(__fastcall *)(const void *node, uint32_t *bgra, void *status);
    const uintptr_t result = Frame::Subclass::Slot<LoadXML_t>(
        Offsets::VTBL_SIMPLEFRAME_LAYOUT, Offsets::VTBL_XML_NODE_LOADER)(layout, nullptr, node, status);
    if (node == nullptr)
        return result;
    void *frame = OwnerOf(layout);
    if (const char *layer = Xml::Attr(node, "drawLayer"); layer != nullptr && *layer != '\0')
        reinterpret_cast<LayerFromName_t>(Offsets::FUN_XML_DRAW_LAYER_FROM_NAME)(layer, &F(frame).drawLayer);
    float value = 0.0f;
    if (Xml::FloatAttr(node, "fogNear", value))
        Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR) = value < 0.0f ? 0.0f : value;
    if (Xml::FloatAttr(node, "fogFar", value))
        Game::Ref<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) = value < 0.0f ? 0.0f : value;
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c)) {
        if (Xml::TagIs(c, "FogColor")) {
            reinterpret_cast<ReadColor_t>(Offsets::FUN_XML_READ_COLOR)(
                c, Game::Ptr<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR), status);
            FogFlags(frame) |= Offsets::SIMPLEMODEL_FOG_ARMED;
        } else if (Xml::TagIs(c, "ViewInsets")) {
            float *in = F(frame).viewInsets;
            Xml::FloatAttr(c, "left", in[0]);
            Xml::FloatAttr(c, "right", in[1]);
            Xml::FloatAttr(c, "top", in[2]);
            Xml::FloatAttr(c, "bottom", in[3]);
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
    SetOrientation(f, 0.0f, 0.0f, 0.0f);
    f.fieldOfView = kDefaultFieldOfView;
    f.nearClip = kDefaultNearClip;
    f.farClip = kDefaultFarClip;
    f.drawLayer = Offsets::DRAWLAYER_ARTWORK;
    f.groundTerrain = -1;
    f.hovered = nullptr;
    f.world = nullptr;
    f.playWorldModelSounds = false;
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

int __fastcall Script_CreateActor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const char *name = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    const void *templateNode = nullptr;
    if (Game::Lua::IsString(L, 3)) {
        const char *inherits = Game::Lua::ToString(L, 3);
        templateNode = Xml::Template(inherits);
        if (templateNode == nullptr) {
            Game::Lua::Error(L, "%s:CreateActor(): Couldn't find inherited node \"%s\"",
                             Game::Lua::ObjectDisplayName(frame), inherits);
            return 0;
        }
    }
    Actor *actor = new Actor(frame, name);
    F(frame).actors->push_back(actor);
    if (templateNode != nullptr)
        actor->LoadXml(templateNode, nullptr);
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

int __fastcall Script_GetActorAtIndex(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, Game::Lua::ObjectDisplayName(frame),
                                        "GetActorAtIndex(index)"))
        return 0;
    const std::vector<Actor *> &actors = *F(frame).actors;
    const double index = Game::Lua::ToNumber(L, 2);
    if (!(index >= 1.0 && index <= static_cast<double>(actors.size())))
        return 0;
    Animation::Push(L, actors[static_cast<size_t>(index) - 1]);
    return 1;
}

int __fastcall Script_SetCameraPosition(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, Game::Lua::ObjectDisplayName(frame),
                                        "SetCameraPosition(x, y, z)"))
        return 0;
    F(frame).position = Vec3Args(L, 2);
    ApplyCamera(frame);
    return 0;
}

int __fastcall Script_GetCameraPosition(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).position) : 0;
}

int __fastcall Script_SetCameraOrientationByYawPitchRoll(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 3, Game::Lua::ObjectDisplayName(frame),
                    "SetCameraOrientationByYawPitchRoll(yaw, pitch, roll)"))
        return 0;
    SetOrientation(F(frame), FloatArg(L, 2), FloatArg(L, 3), FloatArg(L, 4));
    ApplyCamera(frame);
    return 0;
}

int __fastcall Script_SetCameraOrientationByAxisVectors(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 9, Game::Lua::ObjectDisplayName(frame),
                    "SetCameraOrientationByAxisVectors(forwardX, forwardY, forwardZ, rightX, "
                    "rightY, rightZ, upX, upY, upZ)"))
        return 0;
    Fields &f = F(frame);
    f.forward = Vec3Args(L, 2);
    f.right = Vec3Args(L, 5);
    f.up = Vec3Args(L, 8);
    ApplyCamera(frame);
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

// The one-number camera setters and getters.
constexpr char kUsageSetFieldOfView[] = "SetCameraFieldOfView(fov)";
constexpr char kUsageSetNearClip[] = "SetCameraNearClip(nearClip)";
constexpr char kUsageSetFarClip[] = "SetCameraFarClip(farClip)";

template <float Fields::*Field, const char *Usage>
int __fastcall SetCameraScalar(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, Game::Lua::ObjectDisplayName(frame), Usage))
        return 0;
    F(frame).*Field = FloatArg(L, 2);
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
        !reinterpret_cast<LayerFromName_t>(Offsets::FUN_XML_DRAW_LAYER_FROM_NAME)(
            Game::Lua::ToString(L, 2), &layer)) {
        Game::Lua::Error(L, "Usage: %s:SetDrawLayer(layer)", Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
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

int __fastcall Script_SetPaused(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    F(frame).paused = Game::Lua::ToBoolean(L, 2) != 0 ? 1 : 0;
    return 0;
}

int __fastcall Script_Project3DPointTo2D(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, Game::Lua::ObjectDisplayName(frame),
                                        "Project3DPointTo2D(pointX, pointY, pointZ)"))
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

int __fastcall Script_SetViewInsets(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 4, Game::Lua::ObjectDisplayName(frame),
                                        "SetViewInsets(left, right, top, bottom)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 2, Game::Lua::ObjectDisplayName(frame),
                                        "SetViewTranslation(translationX, translationY)"))
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
    if (!NumberArgs(L, 2, 1, Game::Lua::ObjectDisplayName(frame),
                    "SetGroundTerrain([terrainTypeID])"))
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

// The scene's WMO (a ClassicAPI extension, model/SceneWorld.cpp). A set that
// fails clears it, as an actor's SetModelByPath clears its model.
void SetWorld(void *frame, World::Backdrop *world) {
    World::Free(F(frame).world);
    F(frame).world = world;
}

constexpr char kUsageSetWorldModel[] = "SetWorldModel(path [, x, y, z [, yaw [, doodadSet [, mapID]]]])";

int __fastcall Script_SetWorldModel(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const char *owner = Game::Lua::ObjectDisplayName(frame);
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:%s", owner, kUsageSetWorldModel);
        return 0;
    }
    const auto given = [L](int index) { return Game::Lua::Type(L, index) > Game::Lua::TYPE_NIL; };
    float position[3] = {};
    if (given(3) || given(4) || given(5)) {
        if (!NumberArgs(L, 3, 3, owner, kUsageSetWorldModel))
            return 0;
        position[0] = FloatArg(L, 3);
        position[1] = FloatArg(L, 4);
        position[2] = FloatArg(L, 5);
    }
    for (int index = 6; index <= 8; ++index)
        if (given(index) && !NumberArgs(L, index, 1, owner, kUsageSetWorldModel))
            return 0;
    const float yaw = given(6) ? FloatArg(L, 6) : 0.0f;
    const uint32_t doodadSet = given(7) ? static_cast<uint32_t>(Game::Lua::ToNumber(L, 7)) : 0;
    const uint32_t mapID = given(8) ? static_cast<uint32_t>(Game::Lua::ToNumber(L, 8)) : World::kNoMap;
    float placement[16];
    World::Placement(position, yaw, placement);
    SetWorld(frame, World::Load(Game::Lua::ToString(L, 2), placement, doodadSet, mapID));
    Game::Lua::PushBool(L, F(frame).world != nullptr);
    return 1;
}

int __fastcall Script_SetWorldModelByMapID(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 1, Game::Lua::ObjectDisplayName(frame), "SetWorldModelByMapID(mapID)"))
        return 0;
    SetWorld(frame, World::LoadMap(static_cast<uint32_t>(Game::Lua::ToNumber(L, 2))));
    Game::Lua::PushBool(L, F(frame).world != nullptr);
    return 1;
}

int __fastcall Script_ClearWorldModel(void *L) {
    if (void *frame = Self(L))
        SetWorld(frame, nullptr);
    return 0;
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

int __fastcall Script_SetAllowOverlappedModels(void *L) {
    if (void *frame = Self(L))
        F(frame).allowOverlappedModels = Game::Lua::ToBoolean(L, 2) != 0 ? 1 : 0;
    return 0;
}

int __fastcall Script_GetAllowOverlappedModels(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).allowOverlappedModels != 0);
    return 1;
}

int __fastcall Script_SetLightType(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, Game::Lua::ObjectDisplayName(frame),
                                        "SetLightType(lightType)"))
        return 0;
    SetLightType(frame, Game::Lua::ToNumber(L, 2) != 0.0 ? 1u : 0u);
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
        SetLightVisible(frame, Game::Lua::ToBoolean(L, 2) != 0);
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, Game::Lua::ObjectDisplayName(frame),
                                        "SetLightDirection(x, y, z)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, Game::Lua::ObjectDisplayName(frame),
                                        "SetLightPosition(x, y, z)"))
        return 0;
    SetLightPosition(frame, Vec3Args(L, 2));
    return 0;
}

int __fastcall Script_GetLightPosition(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).lightPosition) : 0;
}

int SetLightColor(void *L, float *(*color)(void *), const char *usage) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, Game::Lua::ObjectDisplayName(frame), usage))
        return 0;
    SetColor(color(frame), Vec3Args(L, 2));
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
    return SetLightColor(L, &AmbientOf, "SetLightAmbientColor(colorR, colorG, colorB)");
}
int __fastcall Script_GetLightAmbientColor(void *L) { return GetLightColor(L, &AmbientOf); }
int __fastcall Script_SetLightDiffuseColor(void *L) {
    return SetLightColor(L, &DiffuseOf, "SetLightDiffuseColor(colorR, colorG, colorB)");
}
int __fastcall Script_GetLightDiffuseColor(void *L) { return GetLightColor(L, &DiffuseOf); }

int __fastcall Script_SetFogColor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, Game::Lua::ObjectDisplayName(frame),
                                        "SetFogColor(colorR, colorG, colorB)"))
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

template <uintptr_t Offset, const char *Usage>
int __fastcall SetFogDistance(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, Game::Lua::ObjectDisplayName(frame), Usage))
        return 0;
    Game::Ref<float>(frame, Offset) = FloatArg(L, 2);
    return 0;
}

template <uintptr_t Offset>
int __fastcall GetFogDistance(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Game::Read<float>(frame, Offset));
    return 1;
}

constexpr char kUsageSetFogNear[] = "SetFogNear(near)";
constexpr char kUsageSetFogFar[] = "SetFogFar(far)";

int __fastcall Script_ClearFog(void *L) {
    if (void *frame = Self(L))
        FogFlags(frame) &= ~static_cast<uint32_t>(Offsets::SIMPLEMODEL_FOG_ARMED);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"CreateActor", &Script_CreateActor},
    {"GetNumActors", &Script_GetNumActors},
    {"GetActorAtIndex", &Script_GetActorAtIndex},
    {"SetCameraPosition", &Script_SetCameraPosition},
    {"GetCameraPosition", &Script_GetCameraPosition},
    {"SetCameraOrientationByYawPitchRoll", &Script_SetCameraOrientationByYawPitchRoll},
    {"SetCameraOrientationByAxisVectors", &Script_SetCameraOrientationByAxisVectors},
    {"GetCameraForward", &Script_GetCameraForward},
    {"GetCameraRight", &Script_GetCameraRight},
    {"GetCameraUp", &Script_GetCameraUp},
    {"SetCameraFieldOfView", &SetCameraScalar<&Fields::fieldOfView, kUsageSetFieldOfView>},
    {"GetCameraFieldOfView", &GetCameraScalar<&Fields::fieldOfView>},
    {"SetCameraNearClip", &SetCameraScalar<&Fields::nearClip, kUsageSetNearClip>},
    {"GetCameraNearClip", &GetCameraScalar<&Fields::nearClip>},
    {"SetCameraFarClip", &SetCameraScalar<&Fields::farClip, kUsageSetFarClip>},
    {"GetCameraFarClip", &GetCameraScalar<&Fields::farClip>},
    {"SetDrawLayer", &Script_SetDrawLayer},
    {"GetDrawLayer", &Script_GetDrawLayer},
    {"SetPaused", &Script_SetPaused},
    {"SetGroundTerrain", &Script_SetGroundTerrain},
    {"GetGroundTerrain", &Script_GetGroundTerrain},
    {"SetWorldModel", &Script_SetWorldModel},
    {"SetWorldModelByMapID", &Script_SetWorldModelByMapID},
    {"ClearWorldModel", &Script_ClearWorldModel},
    {"SetPlayWorldModelSounds", &Script_SetPlayWorldModelSounds},
    {"GetPlayWorldModelSounds", &Script_GetPlayWorldModelSounds},
    {"SetAllowOverlappedModels", &Script_SetAllowOverlappedModels},
    {"GetAllowOverlappedModels", &Script_GetAllowOverlappedModels},
    {"Project3DPointTo2D", &Script_Project3DPointTo2D},
    {"GetActorAtCursor", &Script_GetActorAtCursor},
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
    {"SetFogNear", &SetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_NEAR, kUsageSetFogNear>},
    {"GetFogNear", &GetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_NEAR>},
    {"SetFogFar", &SetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_FAR, kUsageSetFogFar>},
    {"GetFogFar", &GetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_FAR>},
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
    Opt("affectsGlobalPause", "bool", "true", "Has no effect."),
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
const Game::Doc::Function kSetPaused{"Stops or restarts the actors' animations.", kPausedArgs, {}};
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
    "scene's light stands in for the sun. A WMO with fewer than two fogs takes the scene's fog. Returns "
    "false, and shows no WMO, when the file is missing or is not a WMO.",
    kWorldModelArgs, kWorldModelRet, nullptr, true};
const Game::Doc::Function kSetWorldModelByMapID{
    "Shows the WMO of a map that is one WMO, placed as the map places it, so scene positions are the "
    "map's positions. Returns false, and shows no WMO, for any other map.",
    kWorldModelMapArg, kWorldModelRet, nullptr, true};
const Game::Doc::Function kClearWorldModel{"Removes the scene's WMO.", {}, {}, nullptr, true};
const Game::Doc::Field kWorldModelSoundsArg[] = {Req("enabled", "bool")};
const Game::Doc::Function kSetPlayWorldModelSounds{
    "Sets whether the WMO's props play their sounds, as they do in the world, heard from the scene's "
    "camera: a torch is heard near it and not far away. Only in the world, not at the glue screen. Off by "
    "default.",
    kWorldModelSoundsArg, {}, nullptr, true};
const Game::Doc::Function kGetPlayWorldModelSounds{
    "Returns whether the WMO's props play their sounds.", {}, kWorldModelSoundsArg, nullptr, true};
const Game::Doc::Field kOverlappedArg[] = {Req("allowOverlappedModels", "bool")};
const Game::Doc::Field kOverlappedRet[] = {Req("allowOverlappedModels", "bool")};
const Game::Doc::Function kSetAllowOverlappedModels{
    "Stores the flag. Scenes that overlap other model frames always draw.", kOverlappedArg, {}};
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
const Game::Doc::Function kSetFogColor{"Sets the fog colour and turns the fog on.", kColorArgs, {}};
const Game::Doc::Function kGetFogColor{"Returns the fog colour.", {}, kColorRets};
const Game::Doc::Function kSetFogNear{"Sets the distance where the fog starts.", kNearFogArg, {}};
const Game::Doc::Function kGetFogNear{"Returns the distance where the fog starts.", {},
                                      kNearFogRet};
const Game::Doc::Function kSetFogFar{"Sets the distance where the fog is full.", kFarFogArg, {}};
const Game::Doc::Function kGetFogFar{"Returns the distance where the fog is full.", {},
                                     kFarFogRet};
const Game::Doc::Function kClearFog{"Turns the fog off.", {}, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetDrawLayer", &kSetDrawLayer},
    {"GetDrawLayer", &kGetDrawLayer},
    {"SetPaused", &kSetPaused},
    {"SetAllowOverlappedModels", &kSetAllowOverlappedModels},
    {"GetAllowOverlappedModels", &kGetAllowOverlappedModels},
    {"SetGroundTerrain", &kSetGroundTerrain},
    {"GetGroundTerrain", &kGetGroundTerrain},
    {"SetWorldModel", &kSetWorldModel},
    {"SetWorldModelByMapID", &kSetWorldModelByMapID},
    {"ClearWorldModel", &kClearWorldModel},
    {"SetPlayWorldModelSounds", &kSetPlayWorldModelSounds},
    {"GetPlayWorldModelSounds", &kGetPlayWorldModelSounds},
    {"Project3DPointTo2D", &kProject3DPointTo2D},
    {"GetActorAtCursor", &kGetActorAtCursor},
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

bool NumberArgs(void *L, int first, int count, const char *owner, const char *usage) {
    for (int i = 0; i < count; ++i) {
        if (!Game::Lua::IsNumber(L, first + i)) {
            Game::Lua::Error(L, "Usage: %s:%s", owner, usage);
            return false;
        }
    }
    return true;
}

float FloatArg(void *L, int index) { return static_cast<float>(Game::Lua::ToNumber(L, index)); }

int GroundTerrain(void *sceneFrame) { return F(sceneFrame).groundTerrain; }

World::Backdrop *WorldOf(void *sceneFrame) { return F(sceneFrame).world; }

View CameraView(void *sceneFrame) {
    const Fields &f = F(sceneFrame);
    return {{f.position.x, f.position.y, f.position.z},
            {f.forward.x, f.forward.y, f.forward.z},
            {f.up.x, f.up.y, f.up.z},
            f.farClip};
}

} // namespace Model::Scene
