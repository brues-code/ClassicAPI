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
// visible, then advances the scene; the scene's update does the same for each
// actor's model (Link), in that order. The
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
// A unit's model (SetModelByUnit) is 1.12's pane copy, AttachUnitModel
// (0x005059A0) step for step: the unit's own component pumped (forced), a
// copy of the unit's model in the scene (CreateModelFromModel, which copies
// the attachments: helm, shoulders, held items), its effect / spell
// attachments dropped (DetachAllItemModels), then what DressUpModel's
// SetModelInstance (0x00504350) adds: a clone of the unit's character
// component and its two held-item lanes. The hand grips close as the pane
// closes them. The clone is pumped from the update walk, as the DressUpModel
// paint pumps its own (0x00504470), so Undress / UndressSlot show at once;
// OnModelLoaded runs once its first composite is done. Then retail's
// SetModelByUnit arguments: autoDress off undresses it (ClearModel's steps),
// and the held items are hung in the hands or the sheaths, or left off.
// Dress builds the copy again from the unit. The copy shows the unit as it
// looks: a shapeshifted unit shows its form (1.12 keeps no native-form model
// to copy), so usePlayerNativeForm, holdBowString and customRaceID change
// nothing.
//
// TryOn is DressUpModel's (Model::TryOn's arguments, then TryOnItem's arms,
// Outfit::TryOn) on the actor's component and lanes: a unit's copy or a
// dressed display takes armor and held items, any other model held items
// only. A shapeshifted unit takes no armor, as the pane's ready byte allows
// none. The held items hang per SetSheathed. The pane's worker waits for an
// uncached item record (its cache callback runs it again); the actor waits
// for the record and for its model to load (a display has no component until
// then), and returns DataPending for the first and Success for the second.
// OnModelLoaded runs after the waiting try-ons are on.
//
// SetModelByUnitCreatureDisplayID shows the display the unit shows now
// (UNIT_FIELD_DISPLAYID) the way SetModelByCreatureDisplayID shows one: a
// player shows its race's bare display, a pet its creature's.
//
// SetPaused sets the actor's own pause flag, and the global one too unless
// affectsGlobalPause is false (retail probe, 2026-11-07: (true, false) ->
// true false; then (true) -> true true; then (false, false) -> false true;
// and (false), (true, false), (false) -> false false). Either flag holds
// the actor where the engine's
// own resume reads it from: the key bone's position (GetKeyBoneAnimState,
// 0x00711FE0) once the model has loaded, pinned each update with
// SetKeyBoneAnimTime (0x007127F0), so the
// model stays on the animate list (it still moves, turns and lights) and
// its sequence never reaches its end. Unpaused, it plays on from that frame.
// A new animation while paused holds the new one's first frame. The model's
// global sequences (texture scrolls, some emitters) run on.
//
// A mount actor seats a rider the way the world's Mount (0x00607A00) does:
// the rider's model on the mount's attachment 0, playing the seat animation.
// Attaching takes the rider off the animate list (the mount animates it), and
// the mount holds a reference on it. The rider's placement is then relative to
// the seat. Its scale is the one AttachPlayerToMount requests: 1 over
// CalculateMountScale. The world gives the rider its +0x98 over the mount
// display's scale, and +0x98 is always 1.0, so CalculateMountScale is the mount
// display's CreatureModelScale. DetachFromMount only detaches: the retail probe
// saw the rider keep the seat animation, where the world's Dismount
// (0x00607CE0) plays 0. A mount that drops its model unseats its rider (the
// model's destructor detaches every child), and a rider that drops its own
// leaves the seat first.
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
#include "dbc/Lookup.h"
#include "guid/Guid.h"
#include "item/Data.h"
#include "item/Record.h"
#include "model/DisplayInfo.h"
#include "model/M2.h"
#include "model/VisualKit.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
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
    for (void *effect : actor.kitEffects) {
        M2::Detach(effect);
        M2::Release(effect);
    }
    actor.kitEffects.clear();
    if (actor.model != nullptr) {
        const float white[3] = {1.0f, 1.0f, 1.0f};
        M2::SetDiffuse(actor.model, white);
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
    M2::Detach(actor.model); // a rider leaves its mount
    Model::DisplayInfo::DestroyDresser(actor.dresser);
    M2::Release(actor.model);
    actor.model = nullptr;
    actor.dresser = nullptr;
    actor.dressRecord = nullptr;
    actor.displayScale = 1.0f;
    actor.unitGuid = 0;
    actor.lanes[0] = actor.lanes[1] = Outfit::HeldLane{};
    actor.weapons = Outfit::Weapons{};
    actor.armorReady = true;
    actor.pendingTryOns.clear();
    actor.loaded = false;
    actor.animFinished = false;
    actor.heldPositionMs = -1;
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

bool Actor::SetUnitModel(uint64_t guid) {
    using CloseHand_t = void(__fastcall *)(void *model, uint32_t hand);
    ClearModel();
    void *unit = guid != 0 ? ::Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) : nullptr;
    void *source = unit != nullptr ? Game::Read<void *>(unit, Offsets::OFF_CGOBJECT_MODEL) : nullptr;
    if (source == nullptr)
        return false;
    if (void *own = Game::Read<void *>(unit, Offsets::OFF_CGUNIT_CHARACTER_COMPONENT))
        Model::DisplayInfo::PumpDresser(own, true);
    void *copy = M2::CopyWithoutItems(Model::Pane::Scene(SceneFrame()), source);
    if (copy == nullptr)
        return false;
    unitGuid = guid;
    const auto *descriptor = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    armorReady = Game::Read<uint32_t>(descriptor, Offsets::OFF_UNIT_FIELD_DISPLAYID) ==
                 Game::Read<uint32_t>(descriptor, Offsets::OFF_UNIT_FIELD_NATIVEDISPLAYID);
    dresser = Outfit::CloneComponent(copy, unit);
    Outfit::ReadLanes(unit, lanes);
    Outfit::ReadWeapons(guid, weapons);
    const auto closeHand = reinterpret_cast<CloseHand_t>(Offsets::FUN_CLOSE_HAND);
    if (M2::FindChild(copy, 2) != nullptr)
        closeHand(copy, 1);
    if (M2::FindChild(copy, 1) != nullptr)
        closeHand(copy, 0);
    if (autoDress)
        Outfit::HangWeapons(copy, lanes, weapons, sheathed, hideWeapons);
    else
        Outfit::Undress(copy, dresser, lanes, true);
    // SetModel keeps the unit fields: there is no model to clear.
    SetModel(copy, nullptr, nullptr);
    Model::DisplayInfo::LightChildren(SceneFrame(), copy);
    return true;
}

void Actor::ApplyWeapons() {
    if (model == nullptr)
        return;
    Outfit::HangWeapons(model, lanes, weapons, sheathed, hideWeapons);
    Model::DisplayInfo::LightChildren(SceneFrame(), model);
}

namespace {

// The worker's answer for `record` on the actor: what TryOnItem's gates allow.
int TryOnReason(const Actor &actor, const uint8_t *record) {
    if (actor.model == nullptr ||
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID) == 0)
        return TryOn::kNotEquippable;
    switch (Outfit::ArmOf(Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE))) {
    case Outfit::Arm::Armor: // a component now, or the display's once it loads
        return (actor.dresser != nullptr || actor.dressRecord != nullptr) && actor.armorReady
                   ? TryOn::kSuccess
                   : TryOn::kNotEquippable;
    case Outfit::Arm::Held:
        return TryOn::kSuccess;
    default:
        return TryOn::kNotEquippable;
    }
}

// The worker shows ERR_NOT_EQUIPPABLE for a type it has no arm for.
void ShowIfNotEquippable(const uint8_t *record) {
    if (Outfit::ArmOf(Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE)) ==
        Outfit::Arm::NotEquippable)
        TryOn::ShowNotEquippable();
}

// The worker on the actor's component and lanes.
void PutOn(Actor &actor, const TryOn::Request &request, const uint8_t *record) {
    ShowIfNotEquippable(record);
    Outfit::HeldTryOn held;
    held.bodySlot = request.bodySlot;
    held.dualWield = Outfit::CanDualWield(actor.unitGuid);
    held.sheathed = actor.sheathed;
    held.hide = actor.hideWeapons;
    held.enchant = request.enchant;
    if (Outfit::TryOn(actor.model, actor.dresser, actor.armorReady, actor.lanes, actor.weapons,
                      record, held))
        Model::DisplayInfo::LightChildren(actor.SceneFrame(), actor.model);
}

// The waiting try-ons whose record is cached, in the order they came.
void PutOnWaiting(Actor &actor) {
    auto &waiting = actor.pendingTryOns;
    for (size_t i = 0; i < waiting.size();) {
        const TryOn::Request request = waiting[i];
        const uint8_t *record = Item::PeekRecord(TryOn::WornItem(request.itemID));
        if (record == nullptr) {
            ++i;
            continue;
        }
        waiting.erase(waiting.begin() + static_cast<std::ptrdiff_t>(i));
        PutOn(actor, request, record);
    }
}

} // namespace

int Actor::TryOnItem(const Model::TryOn::Request &request) {
    if (model == nullptr)
        return TryOn::kNotEquippable;
    const uint8_t *record = Item::PeekRecord(TryOn::WornItem(request.itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(request.itemID);
        pendingTryOns.push_back(request);
        return TryOn::kDataPending;
    }
    if (loaded) {
        PutOn(*this, request, record);
        return TryOnReason(*this, record);
    }
    const int reason = TryOnReason(*this, record);
    if (reason == TryOn::kSuccess)
        pendingTryOns.push_back(request);
    else
        ShowIfNotEquippable(record);
    return reason;
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
    heldPositionMs = -1; // a paused actor holds the new animation's first frame
    // An end the scene's advance reported for the animation this replaces is
    // not this one's: the engine reports a replaced play as reason 1, which
    // OnAnimDone drops, and a pending end is dropped the same way. Two actors
    // whose animations end in one advance otherwise each see the other's
    // script replace theirs, then run OnAnimFinished for the new one.
    animFinished = false;
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
        if (ready && dressRecord != nullptr && dresser == nullptr) {
            // A dress the engine rejects shows the model undressed, as a
            // unit with no compositor shows.
            dresser = Model::DisplayInfo::CreateDresser(model, dressRecord);
            if (dresser == nullptr)
                dressRecord = nullptr;
            else
                Model::DisplayInfo::LightChildren(SceneFrame(), model);
        }
        if (ready && dresser != nullptr && !Model::DisplayInfo::PumpDresser(dresser))
            ready = false;
        if (ready && M2::IsLoaded(model, 0, 1)) {
            loaded = true;
            PutOnWaiting(*this);
            ApplyKit(*this);
            RunScript(S_ONMODELLOADED, 0);
        }
    } else {
        if (!pendingTryOns.empty())
            PutOnWaiting(*this);
        if (dresser != nullptr)
            Model::DisplayInfo::PumpDresser(dresser);
    }
    if (model != nullptr && loaded && (paused || globalPaused)) {
        if (heldPositionMs < 0)
            heldPositionMs = M2::GetAnimPosition(model, -1);
        M2::SetAnimPosition(model, -1, heldPositionMs);
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

// The Model frame's clock (0x0076D7F0) links its ready model into the animate
// list before it advances the scene: the advance (M2SceneAdvance, 0x007074D8)
// updates only the models on the list, and that update is what queues the
// end-of-sequence callback, while the paint's animate pass empties the list.
void Actor::Link() {
    if (model == nullptr || !M2::IsReady(model, 0, 1))
        return;
    if (M2::Parent(model) == nullptr) // a seated rider is animated by its mount
        M2::SetAnimated(model, true);
    M2::SetVisible(model, shown);
}

// A seated rider is placed the same way, relative to its seat: the world's
// Mount gives the attached rider its base transform too.
void Actor::Place() {
    if (model == nullptr)
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

// Shows a creature display, dressed by its CreatureDisplayInfoExtra record.
bool ShowCreatureDisplay(Actor &actor, uint32_t displayID) {
    const char *path = nullptr;
    void *model = Model::DisplayInfo::CreateDisplayModel(Model::Pane::Scene(actor.SceneFrame()),
                                                         displayID, &path);
    actor.SetModel(model, path, Model::DisplayInfo::DressRecord(displayID));
    if (model != nullptr)
        actor.displayScale = Game::Read<float>(
            DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                        Offsets::VAR_CREATUREDISPLAYINFO_COUNT, displayID),
            Offsets::OFF_CREATUREDISPLAYINFO_SCALE);
    return model != nullptr;
}

int __fastcall Script_SetModelByCreatureDisplayID(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr ||
        !NumberArgs(L, 2, 1, actor->DisplayName(),
                    "SetModelByCreatureDisplayID(creatureDisplayID)"))
        return 0;
    Game::Lua::PushBool(L, ShowCreatureDisplay(*actor, static_cast<uint32_t>(Game::Lua::ToNumber(L, 2))));
    return 1;
}

// The display the unit shows now (UNIT_FIELD_DISPLAYID), as a creature
// display: no copy of the unit, so nothing it wears beyond the display's own
// dress.
int __fastcall Script_SetModelByUnitCreatureDisplayID(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetModelByUnitCreatureDisplayID(unit)", actor->DisplayName());
        return 0;
    }
    void *unit = Game::ResolveUnitToken(Game::Lua::ToString(L, 2));
    if (unit == nullptr) {
        actor->ClearModel();
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const auto displayID = Game::Read<uint32_t>(Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR),
                                                Offsets::OFF_UNIT_FIELD_DISPLAYID);
    Game::Lua::PushBool(L, ShowCreatureDisplay(*actor, displayID));
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

// Retail's argument order: unit, sheatheWeapons, autoDress, hideWeapons,
// usePlayerNativeForm, holdBowString, customRaceID.
int __fastcall Script_SetModelByUnit(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetModelByUnit(unit [, sheatheWeapons, autoDress, hideWeapons])",
                         actor->DisplayName());
        return 0;
    }
    actor->sheathed = Game::Lua::ToBoolean(L, 3) != 0;
    actor->autoDress = Game::Lua::Type(L, 4) <= Game::Lua::TYPE_NIL || Game::Lua::ToBoolean(L, 4) != 0;
    actor->hideWeapons = Game::Lua::ToBoolean(L, 5) != 0;
    const uint64_t guid = Unit::Identity::GuidForToken(Game::Lua::ToString(L, 2));
    Game::Lua::PushBool(L, actor->SetUnitModel(guid));
    return 1;
}

int __fastcall Script_GetModelUnitGUID(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || actor->unitGuid == 0)
        return 0;
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(actor->unitGuid, buf, sizeof buf));
    return 1;
}

// Builds the copy again from the unit, dressed.
int __fastcall Script_Dress(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || actor->unitGuid == 0)
        return 0;
    const bool autoDress = actor->autoDress;
    actor->autoDress = true;
    actor->SetUnitModel(actor->unitGuid);
    actor->autoDress = autoDress;
    return 0;
}

int __fastcall Script_Undress(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    const bool weapons = Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL || Game::Lua::ToBoolean(L, 2) != 0;
    Outfit::Undress(actor->model, actor->dresser, actor->lanes, weapons);
    return 0;
}

int __fastcall Script_UndressSlot(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "UndressSlot(inventorySlot)"))
        return 0;
    Outfit::UndressSlot(actor->model, actor->dresser, actor->lanes,
                        static_cast<int>(Game::Lua::ToNumber(L, 2)));
    return 0;
}

int __fastcall Script_TryOn(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    const TryOn::Request request = TryOn::ReadArgs(L);
    if (request.itemID == 0) {
        TryOn::ShowNotEquippable();
        Game::Lua::PushNumber(L, TryOn::kNotEquippable);
        return 1;
    }
    Game::Lua::PushNumber(L, actor->TryOnItem(request));
    return 1;
}

int __fastcall Script_SetSheathed(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    actor->sheathed = Game::Lua::ToBoolean(L, 2) != 0;
    actor->hideWeapons = Game::Lua::ToBoolean(L, 3) != 0;
    actor->ApplyWeapons();
    return 0;
}

int __fastcall Script_GetSheathed(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->sheathed);
    return 1;
}

int __fastcall Script_SetAutoDress(void *L) {
    if (Actor *actor = Self(L))
        actor->autoDress = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __fastcall Script_GetAutoDress(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->autoDress);
    return 1;
}

// Holds the actor's animation at its current frame; the scene keeps running.
int __fastcall Script_SetPaused(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    actor->paused = Game::Lua::ToBoolean(L, 2) != 0;
    if (Game::Lua::Type(L, 3) <= Game::Lua::TYPE_NIL || Game::Lua::ToBoolean(L, 3) != 0)
        actor->globalPaused = actor->paused;
    actor->heldPositionMs = -1;
    return 0;
}

int __fastcall Script_GetPaused(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->paused);
    Game::Lua::PushBool(L, actor->globalPaused);
    return 2;
}

int __fastcall Script_IsGeoReady(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->model != nullptr && M2::IsLoaded(actor->model, 0, 1));
    return 1;
}

// The rider argument of the mount methods.
Actor *RiderArg(void *L, const Actor *mount, const char *usage) {
    Actor *rider = static_cast<Actor *>(Animation::Resolve(L, 2, Actor::s_class));
    if (rider == nullptr)
        Game::Lua::Error(L, "Usage: %s:%s", mount->DisplayName(), usage);
    return rider;
}

// Mount's seating (0x00607A00): the rider on attachment 0, then the seat
// animation. A rider already seated moves to this mount.
int __fastcall Script_AttachToMount(void *L) {
    constexpr char kUsage[] = "AttachToMount(rider, animation [, spellKitVisualID])";
    Actor *mount = Self(L);
    if (mount == nullptr)
        return 0;
    Actor *rider = RiderArg(L, mount, kUsage);
    if (rider == nullptr || !NumberArgs(L, 3, 1, mount->DisplayName(), kUsage))
        return 0;
    const bool seated = rider != mount && mount->model != nullptr && rider->model != nullptr &&
                        rider->SceneFrame() == mount->SceneFrame();
    if (seated) {
        M2::Detach(rider->model);
        M2::Attach(rider->model, mount->model, 0);
        rider->animation = static_cast<uint32_t>(Game::Lua::ToNumber(L, 3));
        rider->PlayAnimation();
        if (Game::Lua::IsNumber(L, 4) && Game::Lua::ToNumber(L, 4) != 0.0)
            rider->SetSpellVisualKit(static_cast<uint32_t>(Game::Lua::ToNumber(L, 4)), false);
    }
    Game::Lua::PushBool(L, seated);
    return 1;
}

int __fastcall Script_DetachFromMount(void *L) {
    Actor *mount = Self(L);
    if (mount == nullptr)
        return 0;
    Actor *rider = RiderArg(L, mount, "DetachFromMount(rider)");
    if (rider == nullptr)
        return 0;
    const bool seated = mount->model != nullptr && rider->model != nullptr &&
                        M2::Parent(rider->model) == mount->model;
    if (seated)
        M2::Detach(rider->model);
    Game::Lua::PushBool(L, seated);
    return 1;
}

// The mount display's scale over the rider's +0x98, which 1.12 keeps at 1.0.
int __fastcall Script_CalculateMountScale(void *L) {
    Actor *mount = Self(L);
    if (mount == nullptr || RiderArg(L, mount, "CalculateMountScale(rider)") == nullptr)
        return 0;
    Game::Lua::PushNumber(L, mount->displayScale);
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
    if (actor == nullptr || !NumberArgs(L, 2, 3, actor->DisplayName(), "SetPosition(x, y, z)"))
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
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), Usage))
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
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), "SetAlpha(alpha)"))
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
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "SetAnimation(animation [, variation, animSpeed, animOffsetSeconds])"))
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
        !NumberArgs(L, 2, 1, actor->DisplayName(), "SetAnimationBlendOperation(blendOperation)"))
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

// Retail's Enum.ModelBlendOperation (ModelAnimationSharedDocumentation.lua);
// the actor stores it as PlayAnimation's blend argument.
const Game::Lua::EnumIntegerEntry kModelBlendOperation[] = {
    {"None", 0},
    {"Anim", 1},
};

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
    {"SetModelByUnit", &Script_SetModelByUnit},
    {"SetModelByUnitCreatureDisplayID", &Script_SetModelByUnitCreatureDisplayID},
    {"GetModelUnitGUID", &Script_GetModelUnitGUID},
    {"Dress", &Script_Dress},
    {"Undress", &Script_Undress},
    {"UndressSlot", &Script_UndressSlot},
    {"TryOn", &Script_TryOn},
    {"SetSheathed", &Script_SetSheathed},
    {"GetSheathed", &Script_GetSheathed},
    {"SetAutoDress", &Script_SetAutoDress},
    {"GetAutoDress", &Script_GetAutoDress},
    {"IsGeoReady", &Script_IsGeoReady},
    {"SetPaused", &Script_SetPaused},
    {"GetPaused", &Script_GetPaused},
    {"AttachToMount", &Script_AttachToMount},
    {"DetachFromMount", &Script_DetachFromMount},
    {"CalculateMountScale", &Script_CalculateMountScale},
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

const Game::Doc::Field kUnitArgs[] = {
    Req("unit", "UnitToken"),
    Opt("sheatheWeapons", "bool", "false"),
    Opt("autoDress", "bool", "true", "False shows the unit with nothing on."),
    Opt("hideWeapons", "bool", "false"),
    Opt("usePlayerNativeForm", "bool", "true", "Has no effect: the unit shows as it looks."),
    Opt("holdBowString", "bool", "false", "Has no effect."),
    Opt("customRaceID", "number", nullptr, "Has no effect."),
};
const Game::Doc::Field kGuidRet[] = {
    Req("guid", "WOWGUID", "Nil when the actor shows no unit."),
};
const Game::Doc::Field kUndressArgs[] = {Opt("includeWeapons", "bool", "true")};
const Game::Doc::Field kUndressSlotArgs[] = {
    Req("inventorySlots", "number", "An inventory slot, 1 (head) to 19 (tabard)."),
};
const Game::Doc::Field kSheathedArgs[] = {
    Req("sheathed", "bool"),
    Opt("hidden", "bool", "false", "True hides the held items."),
};
const Game::Doc::Field kSheathedRet[] = {Req("sheathed", "bool")};
const Game::Doc::Field kAutoDressArg[] = {Req("autoDress", "bool")};
const Game::Doc::Field kAutoDressRet[] = {Req("autoDress", "bool")};
const Game::Doc::Field kGeoReadyRet[] = {Req("isReady", "bool")};
const Game::Doc::Field kUnitTokenArg[] = {Req("unit", "UnitToken")};
const Game::Doc::Function kSetModelByUnitCreatureDisplayID{
    "Shows the creature display the unit shows now, dressed like a creature.", kUnitTokenArg,
    kSuccessRet};
const Game::Doc::Field kPausedArgs[] = {
    Req("paused", "bool"),
    Opt("affectsGlobalPause", "bool", "true", "False sets only the actor's own pause."),
};
const Game::Doc::Field kPausedRets[] = {Req("paused", "bool"), Req("globalPaused", "bool")};
const Game::Doc::Function kSetPaused{
    "Holds the actor's animation at its current frame, or lets it play on. Either pause holds it.",
    kPausedArgs, {}};
const Game::Doc::Function kGetPaused{"Returns whether the actor's animation is held.", {},
                                     kPausedRets};
const Game::Doc::Function kSetModelByUnit{
    "Shows a copy of the unit, dressed in what it wears.", kUnitArgs, kSuccessRet};
const Game::Doc::Function kGetModelUnitGUID{"Returns the unit the actor shows.", {}, kGuidRet};
const Game::Doc::Function kDress{"Dresses the actor in what its unit wears.", {}, {}};
const Game::Doc::Function kUndress{"Takes everything off the actor.", kUndressArgs, {}};
const Game::Doc::Function kUndressSlot{
    "Takes the item in one inventory slot off the actor.", kUndressSlotArgs, {}};
const Game::Doc::Field kTryOnArgs[] = {
    Req("linkOrItemModifiedAppearanceID", "IDOrLink",
        "An item ID, item link, trade-skill recipe link, or the name of an item you carry."),
    Opt("handSlotName", "cstring", nullptr,
        "MAINHANDSLOT or SECONDARYHANDSLOT: the hand that holds the item."),
    Opt("spellEnchantID", "number", nullptr,
        "The enchant whose glow a weapon shows. Defaults to the item link's enchant."),
};
const Game::Doc::Field kTryOnRet[] = {Req("result", "ItemTryOnReason")};
const Game::Doc::Function kTryOn{
    "Puts an item on the actor. Armor needs a character model. The item goes on once its data "
    "and the model have loaded.",
    kTryOnArgs, kTryOnRet};
const Game::Doc::Function kSetSheathed{
    "Puts the held items in the hands or the sheaths, or hides them.", kSheathedArgs, {}};
const Game::Doc::Function kGetSheathed{"Returns whether the held items are sheathed.", {},
                                       kSheathedRet};
const Game::Doc::Function kSetAutoDress{
    "Sets whether SetModelByUnit dresses the actor.", kAutoDressArg, {}};
const Game::Doc::Function kGetAutoDress{
    "Returns whether SetModelByUnit dresses the actor.", {}, kAutoDressRet};
const Game::Doc::Function kIsGeoReady{
    "Returns whether the model and the item models on it have loaded.", {}, kGeoReadyRet};

const Game::Doc::Field kAttachToMountArgs[] = {
    Req("rider", "ModelSceneFrameActor", "Another actor of the scene."),
    Req("animation", "AnimationDataEnum", "The seat animation; 91 for most mounts."),
    Opt("spellKitVisualID", "number", nullptr, "A spell visual kit shown on the rider."),
};
const Game::Doc::Field kRiderArg[] = {Req("rider", "ModelSceneFrameActor")};
const Game::Doc::Field kMountSuccessRet[] = {Req("success", "bool")};
const Game::Doc::Field kMountScaleRet[] = {
    Req("scale", "number", "The rider fits the mount at 1 over this scale."),
};
const Game::Doc::Function kAttachToMount{
    "Seats the rider on this actor. False when either actor shows no model.",
    kAttachToMountArgs, kMountSuccessRet};
const Game::Doc::Function kDetachFromMount{
    "Takes the rider off this actor. The rider keeps its animation. False when the rider is "
    "not seated on this actor.",
    kRiderArg, kMountSuccessRet};
const Game::Doc::Function kCalculateMountScale{
    "Returns the size of this mount relative to the rider: the scale of its creature display.",
    kRiderArg, kMountScaleRet};

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
    {"SetModelByUnit", &kSetModelByUnit},
    {"SetModelByUnitCreatureDisplayID", &kSetModelByUnitCreatureDisplayID},
    {"GetModelUnitGUID", &kGetModelUnitGUID},
    {"Dress", &kDress},
    {"Undress", &kUndress},
    {"UndressSlot", &kUndressSlot},
    {"TryOn", &kTryOn},
    {"SetSheathed", &kSetSheathed},
    {"GetSheathed", &kGetSheathed},
    {"SetAutoDress", &kSetAutoDress},
    {"GetAutoDress", &kGetAutoDress},
    {"IsGeoReady", &kIsGeoReady},
    {"SetPaused", &kSetPaused},
    {"GetPaused", &kGetPaused},
    {"AttachToMount", &kAttachToMount},
    {"DetachFromMount", &kDetachFromMount},
    {"CalculateMountScale", &kCalculateMountScale},
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
    Game::Lua::RegisterIntegerEnum("Enum", "ModelBlendOperation", kModelBlendOperation,
                                   "ModelAnimationShared");
    Api::Documentation::NameWidgetSystem(&Actor::s_class, "FrameAPIModelSceneFrameActor");
    Api::Documentation::RecordFrameMethods(&Actor::s_class, kMethods, kMethodDocs);
}

} // namespace Model::Scene
