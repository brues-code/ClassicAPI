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
// Vector3DMixin tables (lua/MixinTable.h). From a retail
// probe (docs/notes/model-scene.md § Data): the actor flags split into the
// three useCenterForOrigin booleans, and a NormalizedScale of 0 is a nil
// normalizeScaleAggressiveness; a display ID of 0 is likewise nil.
//
// 3.80.2 (FUN_140AFBC90 registers all eight, Environment "All"): each getter
// reads its ID with the uint32 reader FUN_140A6E2D0. GetModelSceneInfoByID,
// GetModelSceneActorInfoByID and GetModelSceneActorDisplayInfoByID run under
// a protected call that raises the error again as "bad argument #1 to '?'"
// (lua/Arg's Raise); GetModelSceneCameraInfoByID and the four active-scene
// functions are called directly, so their errors name the function and the
// caller's line (RaiseNamed). The active-scene bookkeeping
// (Add/ClearActiveModelScene[Actor], 0x140AFAC50..0x140AFAEE0) does nothing
// past its argument checks. UI_MODEL_SCENE_INFO_UPDATED is reserved: the
// embedded rows never change, so it never fires.

#include "Game.h"
#include "event/Custom.h"
#include "lua/Arg.h"
#include "lua/MixinTable.h"
#include "model/Scene.h"
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

namespace Arg = Game::Lua::Arg;

// The uint32 ID, raised as the function's wrapper raises it.
bool IdArg(void *L, const char *usage, bool named, uint32_t &id) {
    Arg::Error error;
    if (Arg::UInt32(L, 1, usage, id, false, &error))
        return true;
    if (named)
        Arg::RaiseNamed(L, error);
    else
        Arg::Raise(L, error);
    return false;
}

// `t[key]` = a Vector3DMixin of `v` on the table at the top of the stack,
// built as 3.80.2 builds its vector3 returns (lua/MixinTable.h).
void SetFieldVector3D(void *L, const char *key, const Vec3 &v) {
    Game::Lua::PushString(L, key);
    Game::Lua::MixinTable::Push(L, "Vector3DMixin");
    Game::Lua::SetFieldNumber(L, "x", v.x);
    Game::Lua::SetFieldNumber(L, "y", v.y);
    Game::Lua::SetFieldNumber(L, "z", v.z);
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
    if (!IdArg(L,
               "Usage: local modelSceneType, modelCameraIDs, modelActorsIDs, flags = "
               "C_ModelInfo.GetModelSceneInfoByID(modelSceneID)",
               false, id))
        return 0;
    const SceneRow *scene = Find(kScenes, id);
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
    if (!IdArg(L,
               "Usage: local modelSceneCameraInfo = C_ModelInfo.GetModelSceneCameraInfoByID(modelSceneCameraID)",
               true, id))
        return 0;
    const CameraRow *camera = Find(kCameras, id);
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
    if (!IdArg(L, "Usage: local actorInfo = C_ModelInfo.GetModelSceneActorInfoByID(modelActorID)", false, id))
        return 0;
    const ActorRow *actor = Find(kActors, id);
    if (actor == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "modelActorID", actor->id);
    Game::Lua::SetFieldString(L, "scriptTag", actor->scriptTag);
    SetFieldVector3D(L, "position", actor->position);
    Game::Lua::SetFieldNumber(L, "yaw", actor->yaw);
    Game::Lua::SetFieldNumber(L, "pitch", actor->pitch);
    Game::Lua::SetFieldNumber(L, "roll", actor->roll);
    if (actor->normalizedScale > 0.0f) // 3.80.2 sets it only above 0
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
    if (!IdArg(L,
               "Usage: local actorDisplayInfo = "
               "C_ModelInfo.GetModelSceneActorDisplayInfoByID(modelActorDisplayID)",
               false, id))
        return 0;
    const ActorDisplayRow *display = Find(kActorDisplays, id);
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

// The active-scene functions: arg 1 a scene (or an actor), arg 2 of the Add
// pair a uint32; nothing else happens.
template <bool Actor, bool WithID>
int __fastcall Script_ActiveModelScene(void *L) {
    static constexpr const char *kUsage[2][2] = {
        {"Usage: C_ModelInfo.ClearActiveModelScene(modelSceneFrame)",
         "Usage: C_ModelInfo.AddActiveModelScene(modelSceneFrame, modelSceneID)"},
        {"Usage: C_ModelInfo.ClearActiveModelSceneActor(modelSceneFrameActor)",
         "Usage: C_ModelInfo.AddActiveModelSceneActor(modelSceneFrameActor, modelSceneActorID)"},
    };
    const char *usage = kUsage[Actor ? 1 : 0][WithID ? 1 : 0];
    Arg::Error error;
    const bool object = Actor ? Model::Scene::IsActor(L, 1) : Model::Scene::IsSceneFrame(L, 1);
    uint32_t id = 0;
    if (!object)
        Arg::Usage(L, 1, usage, &error);
    else if (!WithID || Arg::UInt32(L, 2, usage, id, false, &error))
        return 0;
    Arg::RaiseNamed(L, error);
    return 0;
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

const Game::Doc::Field kSceneFrameArg[] = {Game::Doc::Req("modelSceneFrame", "ModelSceneFrame")};
const Game::Doc::Field kSceneFrameIDArgs[] = {Game::Doc::Req("modelSceneFrame", "ModelSceneFrame"),
                                              Game::Doc::Req("modelSceneID", "number")};
const Game::Doc::Field kSceneActorArg[] = {Game::Doc::Req("modelSceneFrameActor", "ModelSceneFrameActor")};
const Game::Doc::Field kSceneActorIDArgs[] = {Game::Doc::Req("modelSceneFrameActor", "ModelSceneFrameActor"),
                                              Game::Doc::Req("modelSceneActorID", "number")};
const Game::Doc::Function kAddActiveModelScene{"Does nothing past its argument checks.", kSceneFrameIDArgs, {}};
const Game::Doc::Function kAddActiveModelSceneActor{"Does nothing past its argument checks.", kSceneActorIDArgs,
                                                    {}};
const Game::Doc::Function kClearActiveModelScene{"Does nothing past its argument check.", kSceneFrameArg, {}};
const Game::Doc::Function kClearActiveModelSceneActor{"Does nothing past its argument check.", kSceneActorArg, {}};

const Game::Doc::Event kInfoUpdated{"ModelInfo", "Fires when the model scene data changes. This client's scene "
                                                 "data never changes, so it never fires.",
                                    {}};
const Event::Custom::AutoReserve kInfoUpdatedEvent{"UI_MODEL_SCENE_INFO_UPDATED", &kInfoUpdated};

// The 3.80.2 enumerations of UIModelInfoSharedDocumentation.lua and
// UiModelSceneConstantsDocumentation.lua (ItemTryOnReason is DressUp's).
// ModelSceneSetting is the camera rows' flags, which OrbitCameraMixin tests
// on every update (ShouldAlignLightToOrbitDelta); UIModelSceneFlags the
// scene rows' flags.
const Game::Lua::EnumIntegerEntry kModelSceneSetting[] = {
    {"AlignLightToOrbitDelta", 1},
};
const Game::Lua::EnumIntegerEntry kModelSceneType[] = {
    {"MountJournal", 0},
    {"PetJournalCard", 1},
    {"ShopCard", 2},
    {"EncounterJournal", 3},
    {"PetJournalLoadout", 4},
    {"ArtifactTier2", 5},
    {"ArtifactTier2ForgingScene", 6},
    {"ArtifactTier2SlamEffect", 7},
    {"CommentatorVictoryFanfare", 8},
    {"ArtifactRelicTalentEffect", 9},
    {"PvPWarModeOrb", 10},
    {"PvPWarModeFire", 11},
    {"PartyPose", 12},
    {"AzeriteItemLevelUpToast", 13},
    {"AzeritePowers", 14},
    {"AzeriteRewardGlow", 15},
};
const Game::Lua::EnumIntegerEntry kUIModelSceneActorFlag[] = {
    {"Deprecated1", 1},
    {"UseCenterForOriginX", kUseCenterForOriginX},
    {"UseCenterForOriginY", kUseCenterForOriginY},
    {"UseCenterForOriginZ", kUseCenterForOriginZ},
};
const Game::Lua::EnumIntegerEntry kUIModelSceneContext[] = {
    {"None", -1},
    {"PerksProgram", 0},
};
const Game::Lua::EnumIntegerEntry kUIModelSceneFlags[] = {
    {"SheatheWeapon", 1},
    {"HideWeapon", 2},
    {"Autodress", 4},
    {"NoCameraSpin", 8},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "ModelSceneSetting", kModelSceneSetting, "UIModelInfo");
    Game::Lua::RegisterIntegerEnum("Enum", "ModelSceneType", kModelSceneType, "UIModelInfo");
    Game::Lua::RegisterIntegerEnum("Enum", "UIModelSceneActorFlag", kUIModelSceneActorFlag, "UIModelInfo");
    Game::Lua::RegisterIntegerEnum("Enum", "UIModelSceneContext", kUIModelSceneContext, "UiModelSceneConstants");
    Game::Lua::RegisterIntegerEnum("Enum", "UIModelSceneFlags", kUIModelSceneFlags, "UiModelSceneConstants");
    Game::Lua::RegisterTableFunction("C_ModelInfo", "AddActiveModelScene", &Script_ActiveModelScene<false, true>,
                                     &kAddActiveModelScene);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "AddActiveModelSceneActor",
                                     &Script_ActiveModelScene<true, true>, &kAddActiveModelSceneActor);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "ClearActiveModelScene", &Script_ActiveModelScene<false, false>,
                                     &kClearActiveModelScene);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "ClearActiveModelSceneActor",
                                     &Script_ActiveModelScene<true, false>, &kClearActiveModelSceneActor);
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
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

} // namespace Model::ModelInfo
