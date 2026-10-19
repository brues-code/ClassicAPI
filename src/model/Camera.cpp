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

// `PlayerModel:SetPortraitZoom(zoom)`, `SetCamDistanceScale(scale)` and
// `RefreshCamera()` — 4.3.4's CGCharacterModelBase camera methods.
//
// Vanilla's pane camera is ConfigureCamera (FUN_PLAYERMODEL_CONFIGURE_CAMERA),
// run when the pane's model is ready: a frozen copy of the model's camera 1,
// or a fixed camera aimed at the bounding-box centre. 4.3.4's ConfigureCamera
// (CharacterModelBase.cpp, 0x00A03790) is that same choice followed by two
// steps, ported here verbatim onto the installed camera:
//
//   - portrait zoom: move eye, target and field of view from the pane camera
//     toward the model's portrait camera (id 0) by `zoom` — 1 frames the head
//     the way unit-frame portraits do;
//   - distance scale: scale the eye's distance from the target by `scale`,
//     floored at 0.1 and kept at least 0.1 yards out.
//
// Both run in a co-hook after the original, so a pane with the defaults (zoom
// 0, scale 1) keeps vanilla's camera untouched. Vanilla has no room for the
// two values in the pane, so they live in a side table keyed by frame.
//
// Before reading the portrait camera, the model is animated once with the
// frame's facing at 0 — the pass ConfigureCamera itself runs before it copies
// camera 1, and the one the engine's portrait baker (BakePortrait 0x00525200)
// runs before it reads camera 0. 4.3.4 subtracts the frame's position from
// both model cameras; vanilla's ConfigureCamera does not subtract it from
// camera 1, so the portrait camera is used as is, in the same space.

#include "Game.h"
#include "Offsets.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Model::Camera {
namespace {

struct Settings {
    float portraitZoom = 0.0f;
    float distanceScale = 1.0f;
};
std::unordered_map<void *, Settings> g_settings;

using GetVector_t = void(__fastcall *)(void *camera, uint32_t id, float *out);
using GetScalar_t = float(__fastcall *)(void *camera, uint32_t id);
using SetVector_t = void(__fastcall *)(void *camera, uint32_t id, const float *value,
                                       uint32_t keep);
using SetScalar_t = void(__fastcall *)(void *camera, uint32_t id, float value);
using GetCameraCount_t = uint32_t(__thiscall *)(void *model);
using GetCamera_t = void *(__thiscall *)(void *model, uint32_t index);
using LookupIndex_t = uint16_t(__fastcall *)(uint32_t id, const void *table);
using SetAnimated_t = void(__thiscall *)(void *model, int animated);
using GetScene_t = void *(__thiscall *)(void *frame);
using SceneAnimate_t = void(__thiscall *)(void *scene, const float *cameraPos);
using UpdateModel_t = void(__thiscall *)(void *frame);
using ModelLoaded_t = int(__thiscall *)(void *model, int tryLoad, int recurseChildren);
using ConfigureCamera_t = void(__thiscall *)(void *pane);

struct Vec3 {
    float x, y, z;
};

Vec3 GetVector(void *camera, uint32_t id) {
    Vec3 v{};
    reinterpret_cast<GetVector_t>(Offsets::FUN_CAMERA_GET_VECTOR)(camera, id, &v.x);
    return v;
}

void SetVector(void *camera, uint32_t id, const Vec3 &v) {
    reinterpret_cast<SetVector_t>(Offsets::FUN_CAMERA_SET_VECTOR)(camera, id, &v.x, 0);
}

float GetScalar(void *camera, uint32_t id) {
    return reinterpret_cast<GetScalar_t>(Offsets::FUN_CAMERA_GET_SCALAR)(camera, id);
}

void SetScalar(void *camera, uint32_t id, float value) {
    reinterpret_cast<SetScalar_t>(Offsets::FUN_CAMERA_SET_SCALAR)(camera, id, value);
}

// The model's camera with id `id` (0 = portrait), or null when it has none —
// GetCameraById's lookup, bounded the way 4.3.4 null-checks its result.
void *CameraById(void *model, uint32_t id) {
    const uint32_t count =
        reinterpret_cast<GetCameraCount_t>(Offsets::FUN_M2MODEL_GET_CAMERA_COUNT)(model);
    const uint8_t *data = Game::Read<const uint8_t *>(
        Game::Read<const uint8_t *>(model, Offsets::OFF_M2MODEL_SHARED), Offsets::OFF_M2SHARED_DATA);
    const uint16_t index = reinterpret_cast<LookupIndex_t>(Offsets::FUN_M2_LOOKUP_INDEX)(
        id, data + Offsets::OFF_M2DATA_CAMERA_ID_LOOKUP);
    if (index >= count)
        return nullptr;
    return reinterpret_cast<GetCamera_t>(Offsets::FUN_M2MODEL_GET_CAMERA)(model, index);
}

// ConfigureCamera's own evaluation pass: facing 0, rebuild the root
// transform, animate the scene, restore the facing.
void AnimateUnrotated(void *pane, void *model) {
    float &facing = Game::Ref<float>(pane, Offsets::OFF_SIMPLEMODEL_FACING);
    const float saved = facing;
    facing = 0.0f;
    const void *vtable = Game::Read<const void *>(pane, 0);
    Game::Read<UpdateModel_t>(vtable, Offsets::VTBL_SIMPLEMODEL_UPDATE_MODEL)(pane);
    reinterpret_cast<SetAnimated_t>(Offsets::FUN_M2MODEL_SET_ANIMATED)(model, 1);
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    void *scene = reinterpret_cast<GetScene_t>(Offsets::FUN_SIMPLEMODEL_GET_SCENE)(pane);
    reinterpret_cast<SceneAnimate_t>(Offsets::FUN_M2SCENE_ANIMATE)(scene, origin);
    facing = saved;
}

Vec3 Lerp(const Vec3 &from, const Vec3 &to, float t) {
    return {(to.x - from.x) * t + from.x, (to.y - from.y) * t + from.y,
            (to.z - from.z) * t + from.z};
}

// 4.3.4 ConfigureCamera's last two steps, on the camera vanilla installed.
void ApplySettings(void *pane, const Settings &s) {
    void *model = Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
    void *camera = Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODEL_CAMERA);
    if (model == nullptr || camera == nullptr)
        return;

    if (s.portraitZoom != 0.0f) {
        AnimateUnrotated(pane, model);
        if (void *portrait = CameraById(model, 0)) {
            const Vec3 eye = GetVector(camera, Offsets::CAMERA_PROP_EYE);
            const Vec3 target = GetVector(camera, Offsets::CAMERA_PROP_TARGET);
            const float fov = GetScalar(camera, Offsets::CAMERA_PROP_FOV);
            const Vec3 portraitEye = GetVector(portrait, Offsets::CAMERA_PROP_EYE);
            const Vec3 portraitTarget = GetVector(portrait, Offsets::CAMERA_PROP_TARGET);
            const float portraitFov = GetScalar(portrait, Offsets::CAMERA_PROP_FOV);
            SetVector(camera, Offsets::CAMERA_PROP_EYE, Lerp(eye, portraitEye, s.portraitZoom));
            SetVector(camera, Offsets::CAMERA_PROP_TARGET,
                      Lerp(target, portraitTarget, s.portraitZoom));
            SetScalar(camera, Offsets::CAMERA_PROP_FOV,
                      (portraitFov - fov) * s.portraitZoom + fov);
        }
    }

    if (s.distanceScale != 1.0f) {
        const Vec3 eye = GetVector(camera, Offsets::CAMERA_PROP_EYE);
        const Vec3 target = GetVector(camera, Offsets::CAMERA_PROP_TARGET);
        const float scale = s.distanceScale < 0.1f ? 0.1f : s.distanceScale;
        const float dx = eye.x - target.x;
        const float dy = eye.y - target.y;
        const float dz = eye.z - target.z;
        float sx = dx * scale;
        float sy = dy * scale;
        float sz = dz * scale;
        if (sz * sz + sy * sy + sx * sx < 0.010000001f) {
            const float inv = 1.0f / std::sqrt(dy * dy + dz * dz + dx * dx);
            sx = dx * inv * 0.1f;
            sy = dy * inv * 0.1f;
            sz = dz * inv * 0.1f;
        }
        SetVector(camera, Offsets::CAMERA_PROP_EYE, {sx + target.x, sy + target.y, sz + target.z});
    }
}

ConfigureCamera_t g_origConfigureCamera = nullptr;

void __fastcall ConfigureCamera_h(void *pane, void * /*edx*/) {
    g_origConfigureCamera(pane);
    const auto it = g_settings.find(pane);
    if (it != g_settings.end())
        ApplySettings(pane, it->second);
}

const Game::HookAutoRegister _cameraHook{
    Offsets::FUN_PLAYERMODEL_CONFIGURE_CAMERA, reinterpret_cast<void *>(&ConfigureCamera_h),
    reinterpret_cast<void **>(&g_origConfigureCamera)};

// 4.3.4 reconfigures whenever the pane has a model. Here the model must also
// be loaded: ConfigureCamera reads its cameras and bounds, and an unloaded
// model is configured by the ready hook when it arrives, settings included.
void Reconfigure(void *pane) {
    void *model = Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
    if (model == nullptr ||
        !reinterpret_cast<ModelLoaded_t>(Offsets::FUN_MODEL_INSTANCE_LOADED)(model, 0, 0))
        return;
    reinterpret_cast<ConfigureCamera_t>(Offsets::FUN_PLAYERMODEL_CONFIGURE_CAMERA)(pane);
}

void *ResolvePane(void *L) {
    return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_PLAYERMODEL_LUA_TYPE_ID);
}

int __fastcall Script_SetPortraitZoom(void *L) {
    void *pane = ResolvePane(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetPortraitZoom(zoom)");
        return 0;
    }
    const auto zoom = static_cast<float>(Game::Lua::ToNumber(L, 2));
    Settings &s = g_settings[pane];
    if (s.portraitZoom != zoom) {
        s.portraitZoom = zoom;
        Reconfigure(pane);
    }
    return 0;
}

int __fastcall Script_SetCamDistanceScale(void *L) {
    void *pane = ResolvePane(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetCamDistanceScale(scale)");
        return 0;
    }
    const double value = Game::Lua::ToNumber(L, 2);
    if (value <= 0.0) {
        Game::Lua::Error(L, "SetCamDistanceScale: Scale value must be greater than 0");
        return 0;
    }
    const float scale = value >= 0.1 ? static_cast<float>(value) : 0.1f;
    Settings &s = g_settings[pane];
    if (s.distanceScale != scale) {
        s.distanceScale = scale;
        Reconfigure(pane);
    }
    return 0;
}

int __fastcall Script_RefreshCamera(void *L) {
    if (void *pane = ResolvePane(L))
        Reconfigure(pane);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetPortraitZoom", &Script_SetPortraitZoom},
    {"SetCamDistanceScale", &Script_SetCamDistanceScale},
    {"RefreshCamera", &Script_RefreshCamera},
};

const Game::Doc::Field kZoomArgs[] = {
    Game::Doc::Req("zoom", "number", "0 for the full-body camera, 1 for the portrait camera."),
};
const Game::Doc::Function kSetPortraitZoom{
    "Moves the camera between the full-body view and the model's portrait view.", kZoomArgs,
    {}};
const Game::Doc::Field kScaleArgs[] = {
    Game::Doc::Req("scale", "number", "The camera distance multiplier; 1 is the default."),
};
const Game::Doc::Function kSetCamDistanceScale{
    "Scales the camera's distance from the model.", kScaleArgs, {}};
const Game::Doc::Function kRefreshCamera{
    "Configures the camera again for the current model.", {}, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"SetPortraitZoom", &kSetPortraitZoom},
    {"SetCamDistanceScale", &kSetCamDistanceScale},
    {"RefreshCamera", &kRefreshCamera},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Frames are rebuilt on /reload and a new frame can reuse an old address.
void PrepareForReload() {
    g_settings.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace
} // namespace Model::Camera
