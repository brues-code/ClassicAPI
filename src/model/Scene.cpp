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

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "api/Documentation.h"
#include "frame/Subclass.h"
#include "model/Camera.h"
#include "model/M2.h"
#include "xml/FrameLoad.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace Model::Scene {

namespace {

using Frame::Subclass::FrameOfLayout;
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
    using Matrix_t = void(__fastcall *)(float *m);
    const float frameW = rect[3] - rect[1], frameH = rect[2] - rect[0];
    if (!(frameW > 0.0f) || !(frameH > 0.0f))
        return;
    const float sx = (view[3] - view[1]) / frameW;
    const float sy = (view[2] - view[0]) / frameH;
    const float tx = ((view[1] + view[3]) - (rect[1] + rect[3])) / frameW;
    const float ty = ((view[0] + view[2]) - (rect[0] + rect[2])) / frameH;
    float m[16];
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_PROJECTION)(m);
    for (int row = 0; row < 4; ++row) {
        float *r = &m[row * 4];
        r[0] = sx * r[0] + tx * r[3];
        r[1] = sy * r[1] + ty * r[3];
    }
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_SET_PROJECTION)(m);
}

void __fastcall Paint(void *frame) {
    using GetRect_t = int(__thiscall *)(void *layout, float *rect);
    using Matrix_t = void(__fastcall *)(float *m);
    using GetViewport_t = void(__fastcall *)(float *, float *, float *, float *, float *, float *);
    using SetViewport_t = void(__stdcall *)(float, float, float, float, float, float);
    using SetTransforms_t = void(__fastcall *)(void *camera, const float *rect, uint32_t unused);
    using SetupOrtho_t = void(__fastcall *)(const float *rect, const float *point, int unused);
    using DdcToNdc_t = void(__fastcall *)(float *x, float *y, float inX, float inY);
    using Clear_t = void(__fastcall *)(uint32_t mask);
    using Draw_t = void(__thiscall *)(void *scene, uint32_t pass);

    // {bottom, left, top, right}
    float rect[4] = {};
    if (!reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(
            Game::Ptr<uint8_t>(frame, Offsets::OFF_FRAME_LAYOUT_SUBOBJECT), rect))
        return;
    float projection[16];
    float view[16];
    float vp[6]; // minX, maxX, minY, maxY, minZ, maxZ
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_PROJECTION)(projection);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_VIEW)(view);
    reinterpret_cast<GetViewport_t>(Offsets::FUN_GX_GET_VIEWPORT)(&vp[0], &vp[1], &vp[2], &vp[3],
                                                                 &vp[4], &vp[5]);
    float eye[3] = {0.0f, 0.0f, 0.0f};
    if (void *camera = CameraOf(frame)) {
        const Vec3 e = Model::Camera::GetVector(camera, Offsets::CAMERA_PROP_EYE);
        eye[0] = e.x;
        eye[1] = e.y;
        eye[2] = e.z;
        float viewRect[4];
        if (ViewRect(frame, rect, viewRect)) {
            reinterpret_cast<SetTransforms_t>(Offsets::FUN_CAMERA_SET_DEVICE_TRANSFORMS)(camera, viewRect, 0);
            FitProjection(rect, viewRect);
        } else {
            reinterpret_cast<SetTransforms_t>(Offsets::FUN_CAMERA_SET_DEVICE_TRANSFORMS)(camera, rect, 0);
        }
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
        reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(
            rect[1] * vp[1], rect[3] * vp[1], rect[0] * vp[3], rect[2] * vp[3], 0.0f, 1.0f);
        reinterpret_cast<Clear_t>(Offsets::FUN_GX_CLEAR)(2);
        PlaceActors(frame);
        void *scene = Model::Pane::Scene(frame);
        M2::AnimateScene(scene, eye);
        auto draw = reinterpret_cast<Draw_t>(Offsets::FUN_M2SCENE_DRAW);
        draw(scene, 0);
        draw(scene, 1);
    }
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_SET_PROJECTION)(projection);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_SET_VIEW)(view);
    reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(vp[0], vp[1], vp[2], vp[3], vp[4],
                                                                 vp[5]);
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
bool Project(void *frame, const Vec3 &point, double out[3]) {
    using GetRect_t = int(__thiscall *)(void *layout, float *rect);
    using Matrix_t = void(__fastcall *)(float *m);
    using SetTransforms_t = void(__fastcall *)(void *camera, const float *rect, uint32_t unused);

    void *camera = CameraOf(frame);
    float rect[4] = {}; // {bottom, left, top, right}
    if (camera == nullptr ||
        !reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(
            Game::Ptr<uint8_t>(frame, Offsets::OFF_FRAME_LAYOUT_SUBOBJECT), rect))
        return false;
    float savedProjection[16], savedView[16], projection[16], view[16];
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_PROJECTION)(savedProjection);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_VIEW)(savedView);
    reinterpret_cast<SetTransforms_t>(Offsets::FUN_CAMERA_SET_DEVICE_TRANSFORMS)(camera, rect, 0);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_PROJECTION)(projection);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_VIEW)(view);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_SET_PROJECTION)(savedProjection);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_SET_VIEW)(savedView);

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

// ---- the overrides -----------------------------------------------------------------

using DeletingDestructor_t = void *(__fastcall *)(void *self, void *edx, uint32_t flags);
using GetScriptMethod_t = int(__fastcall *)(void *self, void *edx, void *L, const char *name);
using IsA_t = bool(__fastcall *)(void *self, void *edx, int typeId);
using IsObjectType_t = bool(__fastcall *)(void *self, void *edx, const char *name);
using SetFrameAlpha_t = void(__fastcall *)(void *self, void *edx, uint8_t alpha);
using OnFrameUpdate_t = void(__fastcall *)(void *self, void *edx, float elapsedSec);
using EmitLayer_t = void(__fastcall *)(void *self, void *edx, void *batch, int layer);
using Callback_t = void(__fastcall *)(void *arg);
using AddCallback_t = void(__fastcall *)(void *batch, void *edx, Callback_t fn, void *arg);

Frame::Subclass::VTable<Offsets::VTBL_SIMPLEMODEL_SLOTS> g_vtable;
Frame::Subclass::VTable<Offsets::VTBL_SIMPLEMODEL_LAYOUT_SLOTS> g_layoutVtable;
Frame::Subclass::MethodHash g_hash;
int g_typeId = 0;

uintptr_t TypeIdVar() { return reinterpret_cast<uintptr_t>(&g_typeId); }

template <typename Fn>
Fn ModelBase(uintptr_t slot) {
    return Frame::Subclass::Slot<Fn>(Offsets::VTBL_SIMPLEMODEL, slot);
}
template <typename Fn>
Fn FrameBase(uintptr_t slot) {
    return Frame::Subclass::Slot<Fn>(Offsets::VTBL_SIMPLEFRAME, slot);
}

void *__fastcall DeletingDestructor(void *self, void *, uint32_t flags) {
    Fields &f = F(self);
    for (Actor *actor : *f.actors)
        delete actor;
    delete f.actors;
    f.actors = nullptr;
    return ModelBase<DeletingDestructor_t>(Offsets::VTBL_FRAME_DELETING_DESTRUCTOR)(self, nullptr,
                                                                                    flags);
}

void *__fastcall LayoutDeletingDestructor(void *layout, void *, uint32_t flags) {
    return DeletingDestructor(FrameOfLayout(layout), nullptr, flags);
}

int __fastcall GetScriptMethod(void *self, void *, void *L, const char *name) {
    if (g_hash.PushMethod(L, name))
        return 1;
    return FrameBase<GetScriptMethod_t>(Offsets::VTBL_FRAME_GET_SCRIPT_METHOD)(self, nullptr, L,
                                                                               name);
}

bool __fastcall IsA(void *self, void *, int typeId) {
    return (g_typeId != 0 && typeId == g_typeId) ||
           FrameBase<IsA_t>(Offsets::VTBL_FRAME_IS_A)(self, nullptr, typeId);
}

bool __fastcall IsObjectType(void *self, void *, const char *name) {
    return (name != nullptr && Game::StrCmpI(name, "ModelScene") == 0) ||
           FrameBase<IsObjectType_t>(Offsets::VTBL_FRAME_IS_OBJECT_TYPE)(self, nullptr, name);
}

const char *__fastcall GetObjectType(void *, void *) { return "ModelScene"; }

void __fastcall SetFrameAlpha(void *self, void *, uint8_t alpha) {
    ModelBase<SetFrameAlpha_t>(Offsets::VTBL_FRAME_SET_FRAME_ALPHA)(self, nullptr, alpha);
    for (Actor *actor : *F(self).actors)
        actor->ApplyAlpha();
}

void __fastcall OnFrameUpdate(void *self, void *, float elapsedSec) {
    if (F(self).paused)
        FrameBase<OnFrameUpdate_t>(Offsets::VTBL_FRAME_ON_FRAME_UPDATE)(self, nullptr, elapsedSec);
    else
        ModelBase<OnFrameUpdate_t>(Offsets::VTBL_FRAME_ON_FRAME_UPDATE)(self, nullptr, elapsedSec);
    // By index: an actor's script may create another.
    std::vector<Actor *> &actors = *F(self).actors;
    for (size_t i = 0; i < actors.size(); ++i)
        actors[i]->Update(elapsedSec);
}

void __fastcall EmitLayer(void *self, void *, void *batch, int layer) {
    ModelBase<EmitLayer_t>(Offsets::VTBL_FRAME_EMIT_LAYER)(self, nullptr, batch, layer);
    if (layer == F(self).drawLayer)
        reinterpret_cast<AddCallback_t>(Offsets::FUN_RENDERBATCH_ADD_CALLBACK)(batch, nullptr,
                                                                              &Paint, self);
}

void __fastcall UpdateModel(void *self) { PlaceActors(self); }

using LoadXML_t = uintptr_t(__fastcall *)(void *layout, void *edx, const void *node, void *status);

// The frame's own loader, then ModelSceneType's attributes and <FogColor>.
uintptr_t __fastcall LoadXML(void *layout, void *, const void *node, void *status) {
    using LayerFromName_t = int(__fastcall *)(const char *name, int *layer);
    using ReadColor_t = int(__fastcall *)(const void *node, uint32_t *bgra, void *status);
    const uintptr_t result = Frame::Subclass::Slot<LoadXML_t>(
        Offsets::VTBL_SIMPLEFRAME_LAYOUT, Offsets::VTBL_XML_NODE_LOADER)(layout, nullptr, node, status);
    if (node == nullptr)
        return result;
    void *frame = FrameOfLayout(layout);
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
    if (g_vtable.Built())
        return;
    g_vtable.CopyFrom(Offsets::VTBL_SIMPLEMODEL);
    g_layoutVtable.CopyFrom(Offsets::VTBL_SIMPLEMODEL_LAYOUT);
    auto fn = [](auto f) { return reinterpret_cast<const void *>(f); };
    g_vtable.Set(Offsets::VTBL_FRAME_DELETING_DESTRUCTOR, fn(&DeletingDestructor));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_METHOD, fn(&GetScriptMethod));
    g_vtable.Set(Offsets::VTBL_FRAME_SCRIPT_RESOLVER,
                 FrameBase<const void *>(Offsets::VTBL_FRAME_SCRIPT_RESOLVER));
    g_vtable.Set(Offsets::VTBL_FRAME_IS_A, fn(&IsA));
    g_vtable.Set(Offsets::VTBL_FRAME_IS_OBJECT_TYPE, fn(&IsObjectType));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_OBJECT_TYPE, fn(&GetObjectType));
    g_vtable.Set(Offsets::VTBL_FRAME_SET_FRAME_ALPHA, fn(&SetFrameAlpha));
    g_vtable.Set(Offsets::VTBL_FRAME_ON_FRAME_UPDATE, fn(&OnFrameUpdate));
    g_vtable.Set(Offsets::VTBL_FRAME_EMIT_LAYER, fn(&EmitLayer));
    g_vtable.Set(Offsets::VTBL_SIMPLEMODEL_UPDATE_MODEL, fn(&UpdateModel));
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_DELETING_DESTRUCTOR, fn(&LayoutDeletingDestructor));
    g_layoutVtable.Set(Offsets::VTBL_XML_NODE_LOADER, fn(&LoadXML));
}

// The "ModelScene" factory, shaped like the engine's "Model" one (0x006EECF0).
void *__fastcall CreateModelScene(void *parent) {
    using SMemAlloc_t = void *(__stdcall *)(size_t size, const char *file, int line, int flags);
    using Constructor_t = void(__fastcall *)(void *self, void *edx, void *parent);
    BuildVTables();
    void *frame = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC)(
        kObjectSize, __FILE__, __LINE__, 0x08);
    if (frame == nullptr)
        return nullptr;
    reinterpret_cast<Constructor_t>(Offsets::FUN_SIMPLEMODEL_CONSTRUCTOR)(frame, nullptr, parent);
    Game::Ref<const void *>(frame, 0) = g_vtable.slots;
    Game::Ref<const void *>(frame, Offsets::OFF_FRAME_LAYOUT_SUBOBJECT) = g_layoutVtable.slots;
    Fields &f = F(frame);
    f.actors = new std::vector<Actor *>;
    f.position = {0.0f, 0.0f, 0.0f};
    SetOrientation(f, 0.0f, 0.0f, 0.0f);
    f.fieldOfView = kDefaultFieldOfView;
    f.nearClip = kDefaultNearClip;
    f.farClip = kDefaultFarClip;
    f.drawLayer = Offsets::DRAWLAYER_ARTWORK;
    InitLight(frame);
    InitFog(frame);
    InstallCamera(frame);
    return frame;
}

// ---- the methods ------------------------------------------------------------------

void *Self(void *L) { return Game::Lua::ResolveTypedObject(L, 1, TypeIdVar()); }

bool NumberArgs(void *L, int first, int count, void *frame, const char *usage) {
    for (int i = 0; i < count; ++i) {
        if (!Game::Lua::IsNumber(L, first + i)) {
            Game::Lua::Error(L, "Usage: %s:%s", Game::Lua::ObjectDisplayName(frame), usage);
            return false;
        }
    }
    return true;
}

float FloatArg(void *L, int index) { return static_cast<float>(Game::Lua::ToNumber(L, index)); }

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
    if (frame == nullptr || !NumberArgs(L, 2, 1, frame, "GetActorAtIndex(index)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, frame, "SetCameraPosition(x, y, z)"))
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
        !NumberArgs(L, 2, 3, frame, "SetCameraOrientationByYawPitchRoll(yaw, pitch, roll)"))
        return 0;
    SetOrientation(F(frame), FloatArg(L, 2), FloatArg(L, 3), FloatArg(L, 4));
    ApplyCamera(frame);
    return 0;
}

int __fastcall Script_SetCameraOrientationByAxisVectors(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 9, frame,
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
    if (frame == nullptr || !NumberArgs(L, 2, 1, frame, Usage))
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

void NotifyLayerChanged(void *frame, int layer) {
    using Fn_t = void(__fastcall *)(void *self, void *edx, int layer);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRAME_NOTIFY_LAYER_CHANGED)(frame, nullptr, layer);
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
    NotifyLayerChanged(frame, old);
    NotifyLayerChanged(frame, layer);
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, frame, "Project3DPointTo2D(pointX, pointY, pointZ)"))
        return 0;
    double out[3];
    if (!Project(frame, Vec3Args(L, 2), out))
        return 0;
    for (double value : out)
        Game::Lua::PushNumber(L, value);
    return 3;
}

int __fastcall Script_SetViewInsets(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 4, frame, "SetViewInsets(left, right, top, bottom)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 2, frame, "SetViewTranslation(translationX, translationY)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 1, frame, "SetLightType(lightType)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, frame, "SetLightDirection(x, y, z)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, frame, "SetLightPosition(x, y, z)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, frame, usage))
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
    if (frame == nullptr || !NumberArgs(L, 2, 3, frame, "SetFogColor(colorR, colorG, colorB)"))
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
    if (frame == nullptr || !NumberArgs(L, 2, 1, frame, Usage))
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
    {"SetAllowOverlappedModels", &Script_SetAllowOverlappedModels},
    {"GetAllowOverlappedModels", &Script_GetAllowOverlappedModels},
    {"Project3DPointTo2D", &Script_Project3DPointTo2D},
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
    {"Project3DPointTo2D", &kProject3DPointTo2D},
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
    using RegisterFactory_t = int(__fastcall *)(const char *type, void *(__fastcall *)(void *),
                                                int unique);
    // The engine's frame-type table outlives the Lua state, so on a /reload
    // the name is already there and the registrar keeps the first one.
    reinterpret_cast<RegisterFactory_t>(Offsets::FUN_FRAMEXML_REGISTER_FACTORY)(
        "ModelScene", &CreateModelScene, 0);
    Api::Documentation::NameWidgetSystem(g_hash.Registry(), "FrameAPIModelSceneFrame");
    Game::Lua::RegisterFrameMethods(g_hash.Registry(), g_methods, g_methodDocs);
    RegisterActorClass();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Model::Scene
