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
// frame's light callback, as a Model frame's model is.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "api/Documentation.h"
#include "frame/Subclass.h"
#include "model/Camera.h"
#include "model/M2.h"

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

// PlayerModel's light (0x00505680) with retail's values.
void InitLight(void *frame) {
    using SetInt_t = void(__thiscall *)(void *light, int value);
    using SetDirection_t = void(__thiscall *)(void *light, const float *v);
    void *light = Game::Ptr<uint8_t>(frame, Offsets::OFF_SIMPLEMODEL_LIGHT);
    reinterpret_cast<SetInt_t>(Offsets::FUN_M2LIGHT_SET_TYPE)(light, 0);
    const float direction[3] = {0.0f, 1.0f, 0.0f};
    reinterpret_cast<SetDirection_t>(Offsets::FUN_M2LIGHT_SET_DIRECTION)(light, direction);
    float *ambient = Game::Ptr<float>(light, Offsets::OFF_M2LIGHT_AMBIENT);
    float *diffuse = Game::Ptr<float>(light, Offsets::OFF_M2LIGHT_DIFFUSE);
    ambient[0] = ambient[1] = ambient[2] = 178.0f / 255.0f;
    diffuse[0] = diffuse[1] = 204.0f / 255.0f;
    diffuse[2] = 163.0f / 255.0f;
    reinterpret_cast<SetInt_t>(Offsets::FUN_M2LIGHT_SET_ENABLED)(light, 1);
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
        reinterpret_cast<SetTransforms_t>(Offsets::FUN_CAMERA_SET_DEVICE_TRANSFORMS)(camera, rect, 0);
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
    g_layoutVtable.Set(Offsets::VTBL_XML_NODE_LOADER,
                       Frame::Subclass::Slot<const void *>(Offsets::VTBL_SIMPLEFRAME_LAYOUT,
                                                           Offsets::VTBL_XML_NODE_LOADER));
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
    if (Game::Lua::IsString(L, 3)) {
        // Actor templates are XML; no <Actor> loads yet.
        Game::Lua::Error(L, "%s:CreateActor(): Couldn't find inherited node \"%s\"",
                         Game::Lua::ObjectDisplayName(frame), Game::Lua::ToString(L, 3));
        return 0;
    }
    Actor *actor = new Actor(frame, name);
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
constexpr char kSetFieldOfView[] = "SetCameraFieldOfView(fov)";
constexpr char kSetNearClip[] = "SetCameraNearClip(nearClip)";
constexpr char kSetFarClip[] = "SetCameraFarClip(farClip)";

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
    {"SetCameraFieldOfView", &SetCameraScalar<&Fields::fieldOfView, kSetFieldOfView>},
    {"GetCameraFieldOfView", &GetCameraScalar<&Fields::fieldOfView>},
    {"SetCameraNearClip", &SetCameraScalar<&Fields::nearClip, kSetNearClip>},
    {"GetCameraNearClip", &GetCameraScalar<&Fields::nearClip>},
    {"SetCameraFarClip", &SetCameraScalar<&Fields::farClip, kSetFarClip>},
    {"GetCameraFarClip", &GetCameraScalar<&Fields::farClip>},
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

const Game::Doc::Method g_methodDocs[] = {
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
