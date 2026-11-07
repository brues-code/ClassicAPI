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

// `ModelSceneActor`: one model in a ModelScene (FrameAPIModelSceneFrameActorBase-
// Documentation.lua's contract, defaults from the retail probe in
// docs/notes/model-scene.md).
//
// An actor is a script object (Animation::Object) holding one CM2Model in its
// scene frame's own M2 scene (FUN_SIMPLEMODEL_GET_SCENE). The model is built
// the way a Model frame builds its own and lit by the frame's light the same
// way: SetModelInstance (0x0076CD30) gives its model the frame's alpha, the
// done callback and the light/fog callback with the frame as user data, and
// the actor does the same for each of its models. The frame's clock
// (0x0076D7F0) links a ready model into the scene's animate list and makes it
// visible; the actor does that from the scene's update for its own. The
// scene's paint places every actor (Place) where a Model frame rebuilds its one
// root transform (slot 0x98), with CM2Model::SetBaseTransformTRS, so the
// placement is the engine's: scale, then rotate, then translate.
//
//   - Scale is the actor's own: the scene's projection already scales the
//     picture with the frame (model-scene.md A6), so the Model frame's
//     root-scale factors are not applied.
//   - Yaw turns about +Z, the axis SetBaseTransformTRS rotates a Model's
//     facing about. Pitch (about +Y) and roll (about +X) follow it in that
//     order; no retail scene this DLL embeds uses either, and the order is not
//     probed.
//   - useCenterForOrigin moves the model so the centre of its active bounding
//     box on that axis is at the actor's position, before the scale and the
//     rotation.
//   - The active bounding box is the current animation's sequence bounds and
//     the max one the model's overall bounds (MD20 +0xB4), both in model space:
//     the retail probe saw the active box change with the animation and
//     neither move with the actor.
//
// Loading, dressing and the scripts run from the scene frame's update walk,
// where the engine runs OnUpdate, never from the paint or an engine callback:
// the load is polled (IsLoaded), and the done callback only sets a flag.
// A creature display with a CreatureDisplayInfoExtra record is dressed through
// the engine compositor once it loads, as Model::DisplayInfo dresses a
// PlayerModel; OnModelLoaded runs once it is dressed. A finished animation
// plays again (an actor keeps its animation, as PlayerModel re-arms Stand)
// after OnAnimFinished.
//
// A spell visual kit is Model::VisualKit's routine (PlayerModel:
// ApplySpellVisualKit) on the actor's model, applied once the model has
// loaded (attaching waits for the file) and again on each new model; a new
// kit takes the old one's effects off first and resets its tint. 1.12 has no
// animation kits (AnimKit data is a later client's), so PlayAnimationKit and
// StopAnimationKit change nothing.
//
// XML: an <Actor> element is a template CreateActor names (1.12's file loader
// registers every virtual top-level node whatever its tag), loaded the way an
// animation group is: the mixins of the whole inherits chain first, then each
// node from the base up: parentKey / parentArray on the scene, `alpha`,
// `scale`, `file`, <Scripts> and <KeyValues>; then OnLoad. `fileID` and
// `desaturation` load nothing: 1.12 has no file IDs and no model
// desaturation.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "animation/Xml.h"
#include "api/Documentation.h"
#include "model/DisplayInfo.h"
#include "model/M2.h"
#include "model/VisualKit.h"
#include "xml/FrameLoad.h"
#include "xml/Mixin.h"

#include <cmath>
#include <cstdint>

namespace Model::Scene {

namespace {

// Script indexes (kScripts order).
enum ActorScript { S_ONLOAD, S_ONUPDATE, S_ONMODELCLEARED, S_ONMODELLOADING, S_ONMODELLOADED,
                   S_ONANIMFINISHED };
const char *const kScripts[] = {"onload",         "onupdate",      "onmodelcleared",
                                "onmodelloading", "onmodelloaded", "onanimfinished"};

// The engine's done callback: a whole sequence ended (reason 0) — the test
// the Model frame's own (0x0076CDC0) makes before it runs OnAnimFinished.
void __fastcall OnAnimDone(void * /*model*/, uint32_t /*keyBone*/, uint32_t /*animID*/,
                           uint32_t reason, uint32_t /*overshoot*/, uint32_t userLow,
                           uint32_t /*userHigh*/) {
    if (reason == 0)
        reinterpret_cast<Actor *>(static_cast<uintptr_t>(userLow))->animFinished = true;
}

// Takes the kit's effects off the model and undoes its tint.
void DropKitEffects(Actor &actor) {
    using Detach_t = void(__fastcall *)(void *model);
    using SetDiffuse_t = void(__thiscall *)(void *model, const float *rgb);
    for (void *effect : actor.kitEffects) {
        if (M2::Parent(effect) != nullptr)
            reinterpret_cast<Detach_t>(Offsets::FUN_M2MODEL_DETACH)(effect);
        M2::Release(effect);
    }
    actor.kitEffects.clear();
    if (actor.model != nullptr) {
        const float white[3] = {1.0f, 1.0f, 1.0f};
        reinterpret_cast<SetDiffuse_t>(Offsets::FUN_M2MODEL_SET_DIFFUSE_COLOR)(actor.model, white);
        actor.ApplyAlpha();
    }
}

void ApplyKit(Actor &actor) {
    if (actor.model == nullptr || !actor.loaded || actor.spellVisualKit == 0)
        return;
    if (const uint8_t *kit = Model::VisualKit::Record(actor.spellVisualKit))
        Model::VisualKit::Apply(actor.SceneFrame(), actor.model, kit, actor.spellVisualKitOneShot,
                                &actor.kitEffects);
}

// Releases the model without scripts (a destroyed actor runs none).
void ReleaseModel(Actor &actor) {
    if (actor.model == nullptr)
        return;
    DropKitEffects(actor);
    M2::SetAnimDoneCallback(actor.model, nullptr, 0, 0);
    M2::SetVisible(actor.model, false);
    Model::DisplayInfo::DestroyDresser(actor.dresser);
    M2::Release(actor.model);
    actor.model = nullptr;
    actor.dresser = nullptr;
    actor.dressRecord = nullptr;
    actor.loaded = false;
    actor.animFinished = false;
    actor.modelPath.clear();
}

} // namespace

Actor::Actor(void *scene, const char *name) : Object(s_class, scene, name) {}

Actor::~Actor() { ReleaseModel(*this); }

void Actor::SetModel(void *newModel, const char *path, const uint8_t *newDressRecord) {
    ClearModel();
    if (newModel == nullptr)
        return;
    model = newModel;
    modelPath = path != nullptr ? path : "";
    dressRecord = newDressRecord;
    const auto user = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    M2::SetAnimDoneCallback(model, reinterpret_cast<const void *>(&OnAnimDone), user, 0);
    M2::SetRenderCallback(model, reinterpret_cast<const void *>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB),
                          SceneFrame());
    ApplyAlpha();
    PlayAnimation();
    RunScript(S_ONMODELLOADING, 0);
}

void Actor::ClearModel() {
    if (model == nullptr)
        return;
    ReleaseModel(*this);
    RunScript(S_ONMODELCLEARED, 0);
}

void Actor::PlayAnimation() {
    if (model == nullptr)
        return;
    const int offsetMs = static_cast<int>(animOffsetSec * 1000.0f);
    M2::PlayAnimation(model, -1, animation, variation, offsetMs, animSpeed,
                      static_cast<int>(blendOperation), 1);
}

void Actor::SetSpellVisualKit(uint32_t kitID, bool oneShot) {
    DropKitEffects(*this);
    spellVisualKit = kitID;
    spellVisualKitOneShot = oneShot;
    ApplyKit(*this);
}

namespace {

// LoadXml re-enters LoadNode for each inherited template; the mixins of the
// whole chain go in once, first (Xml::Mixin).
void LoadNode(Actor &actor, const void *node, void *status) {
    if (const char *inherits = ::Xml::Attr(node, "inherits"); inherits != nullptr && *inherits != '\0') {
        if (const void *base = ::Xml::Template(inherits))
            LoadNode(actor, base, status);
        else
            ::Xml::Warn(status, "Couldn't find inherited node: %s", inherits);
    }
    Animation::Xml::BindParentKey(actor.SceneFrame(), &actor, ::Xml::Attr(node, "parentKey"));
    Animation::Xml::BindParentArray(actor.SceneFrame(), &actor, ::Xml::Attr(node, "parentArray"));
    float value = 0.0f;
    if (::Xml::FloatAttr(node, "alpha", value)) {
        actor.alpha = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        actor.ApplyAlpha();
    }
    if (::Xml::FloatAttr(node, "scale", value))
        actor.scale = value;
    if (const char *file = ::Xml::Attr(node, "file"); file != nullptr && *file != '\0')
        actor.SetModel(M2::CreateModel(Model::Pane::Scene(actor.SceneFrame()), file), file, nullptr);
    Animation::Xml::LoadScripts(&actor, node);
    Animation::Xml::LoadKeyValues(&actor, node, status);
}

} // namespace

void Actor::LoadXml(const void *node, void *status) {
    if (void *L = Game::Lua::State()) {
        const int top = Game::Lua::GetTop(L);
        Animation::Push(L, this);
        ::Xml::Mixin::ApplyChain(L, top + 1, node, status, DisplayName(), false);
        Game::Lua::SetTop(L, top);
    }
    LoadNode(*this, node, status);
    RunScript(S_ONLOAD, 0);
}

void Actor::ApplyAlpha() {
    if (model == nullptr)
        return;
    const float frameAlpha = Game::Read<uint8_t>(SceneFrame(), Offsets::OFF_FRAME_ALPHA) / 255.0f;
    M2::SetAlpha(model, alpha * frameAlpha);
}

void Actor::Update(float elapsedSec) {
    if (model != nullptr && !loaded) {
        bool ready = M2::IsLoaded(model, 0, 0);
        if (ready && dressRecord != nullptr) {
            if (dresser == nullptr) {
                // A dress the engine rejects shows the model undressed, as a
                // unit with no compositor shows.
                dresser = Model::DisplayInfo::CreateDresser(model, dressRecord);
                if (dresser == nullptr)
                    dressRecord = nullptr;
                else
                    Model::DisplayInfo::LightChildren(SceneFrame(), model);
            }
            if (dresser != nullptr && !Model::DisplayInfo::PumpDresser(dresser))
                ready = false;
        }
        if (ready && M2::IsLoaded(model, 0, 1)) {
            loaded = true;
            ApplyKit(*this);
            RunScript(S_ONMODELLOADED, 0);
        }
    }
    if (model != nullptr && M2::IsReady(model, 0, 1)) {
        M2::SetAnimated(model, true);
        M2::SetVisible(model, shown);
    }
    if (model != nullptr && animFinished) {
        animFinished = false;
        PlayAnimation();
        RunScript(S_ONANIMFINISHED, 0);
    }
    if (HasScript(S_ONUPDATE)) {
        void *L = Game::Lua::State();
        if (L != nullptr) {
            Game::Lua::PushNumber(L, elapsedSec);
            RunScript(S_ONUPDATE, 1);
        }
    }
}

void Actor::Place() {
    if (model == nullptr || M2::Parent(model) != nullptr)
        return;
    using SetTRS_t = void(__thiscall *)(void *model, const float *pos, float angle,
                                        const float *axis, const float *scale);
    using Rotate_t = void(__thiscall *)(float *m, float angle, const float *axis, int);
    using Translate_t = void(__thiscall *)(float *m, const float *t);
    const float axisX[3] = {1.0f, 0.0f, 0.0f};
    const float axisY[3] = {0.0f, 1.0f, 0.0f};
    const float axisZ[3] = {0.0f, 0.0f, 1.0f};
    const float scales[3] = {scale, scale, scale};
    reinterpret_cast<SetTRS_t>(Offsets::FUN_M2MODEL_SET_BASE_TRS)(model, position, yaw, axisZ,
                                                                  scales);
    float *matrix = Game::Ptr<float>(model, Offsets::OFF_M2MODEL_BASE_MATRIX);
    auto rotate = reinterpret_cast<Rotate_t>(Offsets::FUN_MATRIX_ROTATE);
    if (pitch != 0.0f)
        rotate(matrix, pitch, axisY, 1);
    if (roll != 0.0f)
        rotate(matrix, roll, axisX, 1);
    if (!loaded || !(useCenterForOrigin[0] || useCenterForOrigin[1] || useCenterForOrigin[2]))
        return;
    float box[6];
    M2::GetSequenceBounds(model, animation, variation < 0 ? 0u : static_cast<uint32_t>(variation),
                          box);
    float offset[3];
    for (int i = 0; i < 3; ++i)
        offset[i] = useCenterForOrigin[i] ? -(box[i] + box[i + 3]) * 0.5f : 0.0f;
    reinterpret_cast<Translate_t>(Offsets::FUN_MATRIX_TRANSLATE)(matrix, offset);
}

// ---- the methods ---------------------------------------------------------------

namespace {

Actor *Self(void *L) { return static_cast<Actor *>(Animation::Resolve(L, 1, Actor::s_class)); }

bool NumberArgs(void *L, int first, int count, const char *usage, const Actor *actor) {
    for (int i = 0; i < count; ++i) {
        if (!Game::Lua::IsNumber(L, first + i)) {
            Game::Lua::Error(L, "Usage: %s:%s", actor->DisplayName(), usage);
            return false;
        }
    }
    return true;
}

float FloatArg(void *L, int index) { return static_cast<float>(Game::Lua::ToNumber(L, index)); }

int __fastcall Script_SetModelByCreatureDisplayID(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr ||
        !NumberArgs(L, 2, 1, "SetModelByCreatureDisplayID(creatureDisplayID)", actor))
        return 0;
    const auto displayID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    const char *path = nullptr;
    void *model = Model::DisplayInfo::CreateDisplayModel(Model::Pane::Scene(actor->SceneFrame()),
                                                         displayID, &path);
    actor->SetModel(model, path, Model::DisplayInfo::DressRecord(displayID));
    Game::Lua::PushBool(L, model != nullptr);
    return 1;
}

int __fastcall Script_SetModelByPath(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetModelByPath(asset)", actor->DisplayName());
        return 0;
    }
    const char *path = Game::Lua::ToString(L, 2);
    void *model = M2::CreateModel(Model::Pane::Scene(actor->SceneFrame()), path);
    actor->SetModel(model, path, nullptr);
    Game::Lua::PushBool(L, model != nullptr);
    return 1;
}

int __fastcall Script_ClearModel(void *L) {
    if (Actor *actor = Self(L))
        actor->ClearModel();
    return 0;
}

int __fastcall Script_IsLoaded(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->loaded);
    return 1;
}

int __fastcall Script_GetModelPath(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushString(L, actor->modelPath.c_str());
    return 1;
}

int __fastcall Script_SetPosition(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 3, "SetPosition(x, y, z)", actor))
        return 0;
    for (int i = 0; i < 3; ++i)
        actor->position[i] = FloatArg(L, 2 + i);
    return 0;
}

int __fastcall Script_GetPosition(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    for (float v : actor->position)
        Game::Lua::PushNumber(L, v);
    return 3;
}

// The one-number setters and getters.
constexpr char kUsageSetYaw[] = "SetYaw(yaw)";
constexpr char kUsageSetPitch[] = "SetPitch(pitch)";
constexpr char kUsageSetRoll[] = "SetRoll(roll)";
constexpr char kUsageSetScale[] = "SetScale(scale)";

template <float Actor::*Field, const char *Usage>
int __fastcall SetFloat(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, Usage, actor))
        return 0;
    actor->*Field = FloatArg(L, 2);
    return 0;
}

template <float Actor::*Field>
int __fastcall GetFloat(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->*Field);
    return 1;
}

int __fastcall Script_SetAlpha(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, "SetAlpha(alpha)", actor))
        return 0;
    const float a = FloatArg(L, 2);
    actor->alpha = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    actor->ApplyAlpha();
    return 0;
}

int __fastcall Script_SetUseCenterForOrigin(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    for (int i = 0; i < 3; ++i)
        actor->useCenterForOrigin[i] = Game::Lua::ToBoolean(L, 2 + i) != 0;
    return 0;
}

int __fastcall Script_IsUsingCenterForOrigin(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    for (bool b : actor->useCenterForOrigin)
        Game::Lua::PushBool(L, b);
    return 3;
}

void SetShown(Actor &actor, bool shown) {
    actor.shown = shown;
    if (actor.model != nullptr && !shown)
        M2::SetVisible(actor.model, false);
}

int __fastcall Script_Show(void *L) {
    if (Actor *actor = Self(L))
        SetShown(*actor, true);
    return 0;
}

int __fastcall Script_Hide(void *L) {
    if (Actor *actor = Self(L))
        SetShown(*actor, false);
    return 0;
}

int __fastcall Script_SetShown(void *L) {
    if (Actor *actor = Self(L))
        SetShown(*actor, Game::Lua::ToBoolean(L, 2) != 0);
    return 0;
}

int __fastcall Script_IsShown(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->shown);
    return 1;
}

int __fastcall Script_IsVisible(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->shown && Game::Read<int>(actor->SceneFrame(),
                                                           Offsets::OFF_FRAME_VISIBLE) != 0);
    return 1;
}

int __fastcall Script_SetAnimation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, "SetAnimation(animation [, variation, animSpeed, animOffsetSeconds])", actor))
        return 0;
    actor->animation = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    actor->variation = Game::Lua::IsNumber(L, 3) ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : -1;
    actor->animSpeed = Game::Lua::IsNumber(L, 4) ? FloatArg(L, 4) : 1.0f;
    actor->animOffsetSec = Game::Lua::IsNumber(L, 5) ? FloatArg(L, 5) : 0.0f;
    actor->PlayAnimation();
    return 0;
}

int __fastcall Script_GetAnimation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->animation);
    return 1;
}

// Retail pushes the variation unsigned: -1 reads 4294967295.
int __fastcall Script_GetAnimationVariation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(static_cast<uint32_t>(actor->variation)));
    return 1;
}

int __fastcall Script_SetAnimationBlendOperation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr ||
        !NumberArgs(L, 2, 1, "SetAnimationBlendOperation(blendOperation)", actor))
        return 0;
    actor->blendOperation = Game::Lua::ToNumber(L, 2) != 0.0 ? 1u : 0u;
    return 0;
}

int __fastcall Script_GetAnimationBlendOperation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->blendOperation);
    return 1;
}

int __fastcall Script_SetSpellVisualKit(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    const auto kitID =
        Game::Lua::IsNumber(L, 2) ? static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)) : 0u;
    actor->SetSpellVisualKit(kitID, Game::Lua::ToBoolean(L, 3) != 0);
    return 0;
}

int __fastcall Script_GetSpellVisualKit(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->spellVisualKit);
    return 1;
}

// No animation kits in this client: nothing to play or stop.
int __fastcall Script_AnimationKit(void *L) {
    Self(L);
    return 0;
}

// Six numbers, bottom x, y, z then top x, y, z (what the retail getters
// return and ModelSceneActorMixin reads); nothing until the model loads.
int PushBox(void *L, const float (&box)[6]) {
    for (float v : box)
        Game::Lua::PushNumber(L, v);
    return 6;
}

int __fastcall Script_GetActiveBoundingBox(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !actor->loaded)
        return 0;
    float box[6];
    M2::GetSequenceBounds(actor->model, actor->animation,
                          actor->variation < 0 ? 0u : static_cast<uint32_t>(actor->variation), box);
    return PushBox(L, box);
}

int __fastcall Script_GetMaxBoundingBox(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !actor->loaded)
        return 0;
    float box[6];
    M2::GetBoundingBox(actor->model, box);
    return PushBox(L, box);
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"GetObjectType", &Animation::Script_GetObjectType},
    {"IsObjectType", &Animation::Script_IsObjectType},
    {"GetName", &Animation::Script_GetName},
    {"GetParent", &Animation::Script_GetParent},
    {"HasScript", &Animation::Script_HasScript},
    {"GetScript", &Animation::Script_GetScript},
    {"SetScript", &Animation::Script_SetScript},
    {"HookScript", &Animation::Script_HookScript},
    {"SetModelByCreatureDisplayID", &Script_SetModelByCreatureDisplayID},
    {"SetModelByPath", &Script_SetModelByPath},
    {"ClearModel", &Script_ClearModel},
    {"IsLoaded", &Script_IsLoaded},
    {"GetModelPath", &Script_GetModelPath},
    {"SetPosition", &Script_SetPosition},
    {"GetPosition", &Script_GetPosition},
    {"SetYaw", &SetFloat<&Actor::yaw, kUsageSetYaw>},
    {"GetYaw", &GetFloat<&Actor::yaw>},
    {"SetPitch", &SetFloat<&Actor::pitch, kUsageSetPitch>},
    {"GetPitch", &GetFloat<&Actor::pitch>},
    {"SetRoll", &SetFloat<&Actor::roll, kUsageSetRoll>},
    {"GetRoll", &GetFloat<&Actor::roll>},
    {"SetScale", &SetFloat<&Actor::scale, kUsageSetScale>},
    {"GetScale", &GetFloat<&Actor::scale>},
    {"SetAlpha", &Script_SetAlpha},
    {"GetAlpha", &GetFloat<&Actor::alpha>},
    {"SetUseCenterForOrigin", &Script_SetUseCenterForOrigin},
    {"IsUsingCenterForOrigin", &Script_IsUsingCenterForOrigin},
    {"Show", &Script_Show},
    {"Hide", &Script_Hide},
    {"SetShown", &Script_SetShown},
    {"IsShown", &Script_IsShown},
    {"IsVisible", &Script_IsVisible},
    {"SetAnimation", &Script_SetAnimation},
    {"GetAnimation", &Script_GetAnimation},
    {"GetAnimationVariation", &Script_GetAnimationVariation},
    {"GetActiveBoundingBox", &Script_GetActiveBoundingBox},
    {"GetMaxBoundingBox", &Script_GetMaxBoundingBox},
    {"SetAnimationBlendOperation", &Script_SetAnimationBlendOperation},
    {"GetAnimationBlendOperation", &Script_GetAnimationBlendOperation},
    {"SetSpellVisualKit", &Script_SetSpellVisualKit},
    {"GetSpellVisualKit", &Script_GetSpellVisualKit},
    {"PlayAnimationKit", &Script_AnimationKit},
    {"StopAnimationKit", &Script_AnimationKit},
};

// ---- documentation --------------------------------------------------------------

using Game::Doc::Opt;
using Game::Doc::Req;

const Game::Doc::Field kDisplayArgs[] = {
    Req("creatureDisplayID", "number", "A CreatureDisplayInfo ID."),
    Opt("useActivePlayerCustomizations", "bool", "false", "Has no effect."),
};
const Game::Doc::Field kPathArgs[] = {
    Req("asset", "FileAsset", "A model file path."),
    Opt("useMips", "bool", "false", "Has no effect."),
};
const Game::Doc::Field kSuccessRet[] = {
    Req("success", "bool", "False when the actor shows no model."),
};
const Game::Doc::Field kLoadedRet[] = {Req("isLoaded", "bool")};
const Game::Doc::Field kPathRet[] = {Req("path", "string")};
const Game::Doc::Field kXYZArgs[] = {Req("positionX", "number"), Req("positionY", "number"),
                                     Req("positionZ", "number")};
const Game::Doc::Field kXYZRets[] = {Req("positionX", "number"), Req("positionY", "number"),
                                     Req("positionZ", "number")};
const Game::Doc::Field kYawArg[] = {Req("yaw", "number", "Radians about the up axis.")};
const Game::Doc::Field kYawRet[] = {Req("yaw", "number")};
const Game::Doc::Field kPitchArg[] = {Req("pitch", "number", "Radians.")};
const Game::Doc::Field kPitchRet[] = {Req("pitch", "number")};
const Game::Doc::Field kRollArg[] = {Req("roll", "number", "Radians.")};
const Game::Doc::Field kRollRet[] = {Req("roll", "number")};
const Game::Doc::Field kScaleArg[] = {Req("scale", "number")};
const Game::Doc::Field kScaleRet[] = {Req("scale", "number")};
const Game::Doc::Field kAlphaArg[] = {Req("alpha", "number", "0 to 1.")};
const Game::Doc::Field kAlphaRet[] = {Req("alpha", "number")};
const Game::Doc::Field kCenterArgs[] = {Opt("x", "bool", "false"), Opt("y", "bool", "false"),
                                        Opt("z", "bool", "false")};
const Game::Doc::Field kCenterRets[] = {Req("x", "bool"), Req("y", "bool"), Req("z", "bool")};
const Game::Doc::Field kShowArg[] = {Opt("show", "bool", "false")};
const Game::Doc::Field kShownRet[] = {Req("isShown", "bool")};
const Game::Doc::Field kVisibleRet[] = {Req("isVisible", "bool")};
const Game::Doc::Field kAnimationArgs[] = {
    Req("animation", "AnimationDataEnum"),
    Opt("variation", "number", nullptr, "Nil plays any variation."),
    Opt("animSpeed", "number", "1"),
    Opt("animOffsetSeconds", "number", "0"),
};
const Game::Doc::Field kAnimationRet[] = {Req("animation", "AnimationDataEnum")};
const Game::Doc::Field kVariationRet[] = {
    Req("variation", "number", "4294967295 when any variation plays."),
};
const Game::Doc::Field kBoxRets[] = {
    Req("bottomX", "number"), Req("bottomY", "number"), Req("bottomZ", "number"),
    Req("topX", "number"),    Req("topY", "number"),    Req("topZ", "number"),
};

const Game::Doc::Field kBlendArg[] = {Req("blendOperation", "ModelBlendOperation")};
const Game::Doc::Field kBlendRet[] = {Req("blendOperation", "ModelBlendOperation")};
const Game::Doc::Field kKitArgs[] = {
    Opt("spellVisualKitID", "number", "0", "A SpellVisualKit ID; 0 removes the kit."),
    Opt("oneShot", "bool", "false", "True plays the effects once."),
};
const Game::Doc::Field kKitRet[] = {Req("spellVisualKitID", "number")};
const Game::Doc::Field kAnimKitArgs[] = {
    Req("animationKit", "number"),
    Opt("isLooping", "bool", "false"),
};
const Game::Doc::Function kSetAnimationBlendOperation{
    "Sets whether a new animation blends from the one playing.", kBlendArg, {}};
const Game::Doc::Function kGetAnimationBlendOperation{
    "Returns whether a new animation blends from the one playing.", {}, kBlendRet};
const Game::Doc::Function kSetSpellVisualKit{
    "Shows a spell visual kit on the model, in place of the one shown.", kKitArgs, {}};
const Game::Doc::Function kGetSpellVisualKit{"Returns the spell visual kit shown.", {}, kKitRet};
const Game::Doc::Function kPlayAnimationKit{
    "Has no effect: this client has no animation kits.", kAnimKitArgs, {}};
const Game::Doc::Function kStopAnimationKit{
    "Has no effect: this client has no animation kits.", {}, {}};

const Game::Doc::Function kSetModelByCreatureDisplayID{
    "Shows a creature display, dressed like the creature.", kDisplayArgs, kSuccessRet};
const Game::Doc::Function kSetModelByPath{"Shows a model file.", kPathArgs, kSuccessRet};
const Game::Doc::Function kClearModel{"Removes the model.", {}, {}};
const Game::Doc::Function kIsLoaded{"Returns whether the model has loaded.", {}, kLoadedRet};
const Game::Doc::Function kGetModelPath{"Returns the file of the model.", {}, kPathRet};
const Game::Doc::Function kSetPosition{"Moves the actor in the scene.", kXYZArgs, {}};
const Game::Doc::Function kGetPosition{"Returns where the actor stands.", {}, kXYZRets};
const Game::Doc::Function kSetYaw{"Turns the actor about the up axis.", kYawArg, {}};
const Game::Doc::Function kGetYaw{"Returns the yaw.", {}, kYawRet};
const Game::Doc::Function kSetPitch{"Tilts the actor.", kPitchArg, {}};
const Game::Doc::Function kGetPitch{"Returns the pitch.", {}, kPitchRet};
const Game::Doc::Function kSetRoll{"Rolls the actor.", kRollArg, {}};
const Game::Doc::Function kGetRoll{"Returns the roll.", {}, kRollRet};
const Game::Doc::Function kSetScale{"Sets the size.", kScaleArg, {}};
const Game::Doc::Function kGetScale{"Returns the size.", {}, kScaleRet};
const Game::Doc::Function kSetAlpha{"Sets the opacity.", kAlphaArg, {}};
const Game::Doc::Function kGetAlpha{"Returns the opacity.", {}, kAlphaRet};
const Game::Doc::Function kSetUseCenterForOrigin{
    "Sets on which axes the centre of the model stands at the actor position.", kCenterArgs,
    {}};
const Game::Doc::Function kIsUsingCenterForOrigin{
    "Returns on which axes the centre of the model stands at the actor position.", {},
    kCenterRets};
const Game::Doc::Function kShow{"Shows the actor.", {}, {}};
const Game::Doc::Function kHide{"Hides the actor.", {}, {}};
const Game::Doc::Function kSetShown{"Shows or hides the actor.", kShowArg, {}};
const Game::Doc::Function kIsShown{"Returns whether the actor is set to show.", {}, kShownRet};
const Game::Doc::Function kIsVisible{
    "Returns whether the actor is shown and its scene is visible.", {}, kVisibleRet};
const Game::Doc::Function kSetAnimation{
    "Plays an animation on the actor, again each time it ends.", kAnimationArgs, {}};
const Game::Doc::Function kGetAnimation{"Returns the animation.", {}, kAnimationRet};
const Game::Doc::Function kGetAnimationVariation{"Returns the animation variation.", {},
                                                 kVariationRet};
const Game::Doc::Function kGetActiveBoundingBox{
    "Returns the bounds of the model in its current animation. Nothing until it loads.", {},
    kBoxRets};
const Game::Doc::Function kGetMaxBoundingBox{
    "Returns the overall bounds of the model. Nothing until it loads.", {}, kBoxRets};

const Game::Doc::Method kMethodDocs[] = {
    {"SetModelByCreatureDisplayID", &kSetModelByCreatureDisplayID},
    {"SetModelByPath", &kSetModelByPath},
    {"ClearModel", &kClearModel},
    {"IsLoaded", &kIsLoaded},
    {"GetModelPath", &kGetModelPath},
    {"SetPosition", &kSetPosition},
    {"GetPosition", &kGetPosition},
    {"SetYaw", &kSetYaw},
    {"GetYaw", &kGetYaw},
    {"SetPitch", &kSetPitch},
    {"GetPitch", &kGetPitch},
    {"SetRoll", &kSetRoll},
    {"GetRoll", &kGetRoll},
    {"SetScale", &kSetScale},
    {"GetScale", &kGetScale},
    {"SetAlpha", &kSetAlpha},
    {"GetAlpha", &kGetAlpha},
    {"SetUseCenterForOrigin", &kSetUseCenterForOrigin},
    {"IsUsingCenterForOrigin", &kIsUsingCenterForOrigin},
    {"Show", &kShow},
    {"Hide", &kHide},
    {"SetShown", &kSetShown},
    {"IsShown", &kIsShown},
    {"IsVisible", &kIsVisible},
    {"SetAnimation", &kSetAnimation},
    {"GetAnimation", &kGetAnimation},
    {"GetAnimationVariation", &kGetAnimationVariation},
    {"GetActiveBoundingBox", &kGetActiveBoundingBox},
    {"GetMaxBoundingBox", &kGetMaxBoundingBox},
    {"SetAnimationBlendOperation", &kSetAnimationBlendOperation},
    {"GetAnimationBlendOperation", &kGetAnimationBlendOperation},
    {"SetSpellVisualKit", &kSetSpellVisualKit},
    {"GetSpellVisualKit", &kGetSpellVisualKit},
    {"PlayAnimationKit", &kPlayAnimationKit},
    {"StopAnimationKit", &kStopAnimationKit},
};

} // namespace

Animation::Class Actor::s_class{"ModelSceneActor",
                                nullptr,
                                kScripts,
                                static_cast<int>(sizeof(kScripts) / sizeof(kScripts[0])),
                                kMethods,
                                static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0])),
                                0,
                                0};

void RegisterActorClass() {
    Animation::RegisterClass(Actor::s_class);
    Api::Documentation::NameWidgetSystem(&Actor::s_class, "FrameAPIModelSceneFrameActor");
    Api::Documentation::RecordFrameMethods(&Actor::s_class, kMethods, kMethodDocs);
}

} // namespace Model::Scene
