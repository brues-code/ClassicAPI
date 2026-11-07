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

// `C_ModelInfo.GetModelSceneInfoByID(modelSceneID)` -> modelSceneType,
//     modelCameraIDs, modelActorsIDs, flags
// `C_ModelInfo.GetModelSceneCameraInfoByID(modelSceneCameraID)` -> UIModelSceneCameraInfo
// `C_ModelInfo.GetModelSceneActorInfoByID(modelActorID)` -> UIModelSceneActorInfo
// `C_ModelInfo.GetModelSceneActorDisplayInfoByID(modelActorDisplayID)` -> UIModelSceneActorDisplayInfo
//
// Retail's getters read its UiModelScene DB2s; these read the rows embedded
// in model/SceneData.h, shaped as retail returns them. An unknown ID returns
// nothing, as retail's do (MayReturnNothing). The vector3 fields are
// Vector3DMixin objects (CreateVector3D, Util/Vector3D.lua). From a retail
// probe (docs/notes/model-scene.md § Data): the actor flags split into the
// three useCenterForOrigin booleans, and a NormalizedScale of 0 is a nil
// normalizeScaleAggressiveness; a display ID of 0 is likewise nil. Retail's
// active-scene bookkeeping (Add/ClearActiveModelScene[Actor]) does nothing in
// public clients and is the embedded addon's (Compat.lua).

#include "Game.h"
#include "model/SceneData.h"

#include <cstdint>

namespace Model::ModelInfo {

namespace {

using namespace Model::SceneData;

// Enum.UIModelSceneActorFlag.
constexpr uint32_t kUseCenterForOriginX = 0x2;
constexpr uint32_t kUseCenterForOriginY = 0x4;
constexpr uint32_t kUseCenterForOriginZ = 0x8;

template <typename Row, size_t N>
const Row *Find(const Row (&rows)[N], uint32_t id) {
    for (const Row &row : rows)
        if (row.id == id)
            return &row;
    return nullptr;
}

bool IdArg(void *L, uint32_t &id) {
    if (!Game::Lua::IsNumber(L, 1))
        return false;
    id = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    return true;
}

// `t[key] = CreateVector3D(v)` on the table at the top of the stack.
void SetFieldVector3D(void *L, const char *key, const Vec3 &v) {
    Game::Lua::PushString(L, key);
    Game::Lua::PushString(L, "CreateVector3D");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    Game::Lua::PushNumber(L, v.x);
    Game::Lua::PushNumber(L, v.y);
    Game::Lua::PushNumber(L, v.z);
    Game::Lua::Call(L, 3, 1);
    Game::Lua::SetTable(L, -3);
}

// An array of the IDs of every row of `rows` in `sceneID`, in ID order.
template <typename Row, size_t N>
void PushSceneIDs(void *L, const Row (&rows)[N], uint32_t sceneID) {
    Game::Lua::NewTable(L);
    int n = 0;
    for (const Row &row : rows) {
        if (row.sceneID != sceneID)
            continue;
        Game::Lua::PushNumber(L, row.id);
        Game::Lua::RawSetI(L, -2, ++n);
    }
}

int __fastcall Script_GetModelSceneInfoByID(void *L) {
    uint32_t id = 0;
    const SceneRow *scene = IdArg(L, id) ? Find(kScenes, id) : nullptr;
    if (scene == nullptr)
        return 0;
    Game::Lua::PushNumber(L, scene->uiSystemType);
    PushSceneIDs(L, kCameras, id);
    PushSceneIDs(L, kActors, id);
    Game::Lua::PushNumber(L, scene->flags);
    return 4;
}

int __fastcall Script_GetModelSceneCameraInfoByID(void *L) {
    uint32_t id = 0;
    const CameraRow *camera = IdArg(L, id) ? Find(kCameras, id) : nullptr;
    if (camera == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "modelSceneCameraID", camera->id);
    Game::Lua::SetFieldString(L, "scriptTag", camera->scriptTag);
    Game::Lua::SetFieldString(L, "cameraType", "OrbitCamera");
    SetFieldVector3D(L, "target", camera->target);
    Game::Lua::SetFieldNumber(L, "yaw", camera->yaw);
    Game::Lua::SetFieldNumber(L, "pitch", camera->pitch);
    Game::Lua::SetFieldNumber(L, "roll", camera->roll);
    Game::Lua::SetFieldNumber(L, "zoomDistance", camera->zoomDistance);
    Game::Lua::SetFieldNumber(L, "minZoomDistance", camera->minZoomDistance);
    Game::Lua::SetFieldNumber(L, "maxZoomDistance", camera->maxZoomDistance);
    SetFieldVector3D(L, "zoomedTargetOffset", camera->zoomedTargetOffset);
    Game::Lua::SetFieldNumber(L, "zoomedYawOffset", camera->zoomedYawOffset);
    Game::Lua::SetFieldNumber(L, "zoomedPitchOffset", camera->zoomedPitchOffset);
    Game::Lua::SetFieldNumber(L, "zoomedRollOffset", camera->zoomedRollOffset);
    Game::Lua::SetFieldNumber(L, "flags", camera->flags);
    return 1;
}

int __fastcall Script_GetModelSceneActorInfoByID(void *L) {
    uint32_t id = 0;
    const ActorRow *actor = IdArg(L, id) ? Find(kActors, id) : nullptr;
    if (actor == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "modelActorID", actor->id);
    Game::Lua::SetFieldString(L, "scriptTag", actor->scriptTag);
    SetFieldVector3D(L, "position", actor->position);
    Game::Lua::SetFieldNumber(L, "yaw", actor->yaw);
    Game::Lua::SetFieldNumber(L, "pitch", actor->pitch);
    Game::Lua::SetFieldNumber(L, "roll", actor->roll);
    if (actor->normalizedScale != 0.0f)
        Game::Lua::SetFieldNumber(L, "normalizeScaleAggressiveness", actor->normalizedScale);
    Game::Lua::SetFieldBool(L, "useCenterForOriginX", (actor->flags & kUseCenterForOriginX) != 0);
    Game::Lua::SetFieldBool(L, "useCenterForOriginY", (actor->flags & kUseCenterForOriginY) != 0);
    Game::Lua::SetFieldBool(L, "useCenterForOriginZ", (actor->flags & kUseCenterForOriginZ) != 0);
    if (actor->displayID != 0)
        Game::Lua::SetFieldNumber(L, "modelActorDisplayID", actor->displayID);
    return 1;
}

int __fastcall Script_GetModelSceneActorDisplayInfoByID(void *L) {
    uint32_t id = 0;
    const ActorDisplayRow *display = IdArg(L, id) ? Find(kActorDisplays, id) : nullptr;
    if (display == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "animation", display->animation);
    Game::Lua::SetFieldNumber(L, "animationVariation", display->animationVariation);
    Game::Lua::SetFieldNumber(L, "animSpeed", display->animSpeed);
    if (display->animKitID != 0)
        Game::Lua::SetFieldNumber(L, "animationKitID", display->animKitID);
    if (display->spellVisualKitID != 0)
        Game::Lua::SetFieldNumber(L, "spellVisualKitID", display->spellVisualKitID);
    Game::Lua::SetFieldNumber(L, "alpha", display->alpha);
    Game::Lua::SetFieldNumber(L, "scale", display->scale);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kActorDisplayInfoFields[] = {
    Game::Doc::Req("animation", "number"),
    Game::Doc::Req("animationVariation", "number"),
    Game::Doc::Req("animSpeed", "number"),
    Game::Doc::Opt("animationKitID", "number"),
    Game::Doc::Opt("spellVisualKitID", "number"),
    Game::Doc::Req("alpha", "number"),
    Game::Doc::Req("scale", "number"),
};
const Game::Doc::Structure kActorDisplayInfo{"UIModelSceneActorDisplayInfo", "ModelInfo",
                                             kActorDisplayInfoFields};

const Game::Doc::Field kActorInfoFields[] = {
    Game::Doc::Req("modelActorID", "number"),
    Game::Doc::Req("scriptTag", "cstring"),
    Game::Doc::Req("position", "vector3"),
    Game::Doc::Req("yaw", "number"),
    Game::Doc::Req("pitch", "number"),
    Game::Doc::Req("roll", "number"),
    Game::Doc::Opt("normalizeScaleAggressiveness", "number"),
    Game::Doc::Req("useCenterForOriginX", "bool"),
    Game::Doc::Req("useCenterForOriginY", "bool"),
    Game::Doc::Req("useCenterForOriginZ", "bool"),
    Game::Doc::Opt("modelActorDisplayID", "number"),
};
const Game::Doc::Structure kActorInfo{"UIModelSceneActorInfo", "ModelInfo", kActorInfoFields};

const Game::Doc::Field kCameraInfoFields[] = {
    Game::Doc::Req("modelSceneCameraID", "number"),
    Game::Doc::Req("scriptTag", "cstring"),
    Game::Doc::Req("cameraType", "cstring"),
    Game::Doc::Req("target", "vector3"),
    Game::Doc::Req("yaw", "number"),
    Game::Doc::Req("pitch", "number"),
    Game::Doc::Req("roll", "number"),
    Game::Doc::Req("zoomDistance", "number"),
    Game::Doc::Req("minZoomDistance", "number"),
    Game::Doc::Req("maxZoomDistance", "number"),
    Game::Doc::Req("zoomedTargetOffset", "vector3"),
    Game::Doc::Req("zoomedYawOffset", "number"),
    Game::Doc::Req("zoomedPitchOffset", "number"),
    Game::Doc::Req("zoomedRollOffset", "number"),
    Game::Doc::Req("flags", "ModelSceneSetting"),
};
const Game::Doc::Structure kCameraInfo{"UIModelSceneCameraInfo", "ModelInfo", kCameraInfoFields};

const Game::Doc::Field kSceneIDArg[] = {Game::Doc::Req("modelSceneID", "number")};
const Game::Doc::Field kSceneInfoRets[] = {
    Game::Doc::Req("modelSceneType", "ModelSceneType"),
    Game::Doc::Req("modelCameraIDs", "table"),
    Game::Doc::Req("modelActorsIDs", "table"),
    Game::Doc::Req("flags", "number"),
};
const Game::Doc::Function kGetModelSceneInfoByID{
    "Returns a model scene's type and the IDs of its cameras and actors.", kSceneIDArg,
    kSceneInfoRets};

const Game::Doc::Field kCameraIDArg[] = {Game::Doc::Req("modelSceneCameraID", "number")};
const Game::Doc::Field kCameraInfoRet[] = {
    Game::Doc::Req("modelSceneCameraInfo", "UIModelSceneCameraInfo")};
const Game::Doc::Function kGetModelSceneCameraInfoByID{"Returns a model scene camera's settings.",
                                                       kCameraIDArg, kCameraInfoRet};

const Game::Doc::Field kActorIDArg[] = {Game::Doc::Req("modelActorID", "number")};
const Game::Doc::Field kActorInfoRet[] = {Game::Doc::Req("actorInfo", "UIModelSceneActorInfo")};
const Game::Doc::Function kGetModelSceneActorInfoByID{
    "Returns where a model scene actor stands and how it is scaled.", kActorIDArg,
    kActorInfoRet};

const Game::Doc::Field kActorDisplayIDArg[] = {Game::Doc::Req("modelActorDisplayID", "number")};
const Game::Doc::Field kActorDisplayInfoRet[] = {
    Game::Doc::Req("actorDisplayInfo", "UIModelSceneActorDisplayInfo")};
const Game::Doc::Function kGetModelSceneActorDisplayInfoByID{
    "Returns the animation, alpha and scale a model scene actor starts with.",
    kActorDisplayIDArg, kActorDisplayInfoRet};

// Enum.ModelSceneSetting: the camera rows' flags, which OrbitCameraMixin
// tests on every update (ShouldAlignLightToOrbitDelta).
const Game::Lua::EnumIntegerEntry kModelSceneSetting[] = {
    {"AlignLightToOrbitDelta", 1},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "ModelSceneSetting", kModelSceneSetting, "UIModelInfo");
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneInfoByID",
                                     &Script_GetModelSceneInfoByID, &kGetModelSceneInfoByID);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneCameraInfoByID",
                                     &Script_GetModelSceneCameraInfoByID,
                                     &kGetModelSceneCameraInfoByID);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneActorInfoByID",
                                     &Script_GetModelSceneActorInfoByID,
                                     &kGetModelSceneActorInfoByID);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneActorDisplayInfoByID",
                                     &Script_GetModelSceneActorDisplayInfoByID,
                                     &kGetModelSceneActorDisplayInfoByID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Model::ModelInfo
