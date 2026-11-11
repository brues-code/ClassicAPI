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
// Dress builds the copy again from the unit; DressPlayerSlot puts one slot
// back from it: an armor slot copies the unit's component record (so a hidden
// helm or cloak stays off), a hand the unit's lane. The copy shows the unit
// as it looks: a shapeshifted unit shows its form (1.12 keeps no native-form model
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
// OnModelLoaded runs after the waiting try-ons are on. IsSlotAllowed is true
// for the slots TryOn and UndressSlot act on; IsSlotVisible gives the same
// answer, as no 1.12 model hides a slot it allows.
//
// The ItemTransmogInfo methods read and set what each inventory slot wears
// (Outfit::Worn): retail's appearance is the item ID, its illusion the
// enchant whose glow a weapon shows, and the secondary appearance is always
// NoTransmogID (1.12 has neither split shoulders nor paired artifacts). The
// list has every slot 1..19, as retail's TransmogUtil.GetEmptyItemTransmogInfoList
// and its ipairs callers expect. SetItemTransmogInfo is TryOn on those IDs,
// quiet (retail's callers show ERR_NOT_EQUIPPABLE themselves); a 0
// appearance changes nothing, since retail's own transmog UI calls UndressSlot
// for one instead (Blizzard_Transmog.lua). ignoreChildItems names Legion
// artifact parts, which 1.12 has none of.
//
// SetSheathedCategory moves a hand's sheath point by the item's sheathe type,
// which AttachItemModel and DetachHeldItemSlot both turn into the attachment
// (GetSheathAttachment 0x0047A070): Back hangs a hip weapon (type 3) as a
// two-hander (type 1, the back pair), Side a back weapon (1 or 2) at the hip,
// Hide with type 0, which has no sheath point. The lane keeps the type it hangs
// with, so every detach (Undress, UndressSlot, TryOn's eviction) finds it; the
// actor keeps the item's own type beside it. Each lane write (SetModelByUnit,
// TryOn, DressPlayerSlot) puts the item's own type in the lane, then the
// category hangs it again. UseUnitSheatheCategory takes the unit's categories:
// 1.12 units have none, so every hand takes its own sheath point.
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
// ClassicAPI extensions on the same engine parts: AttachToActor hangs the
// actor on any attachment of another (AttachToMount without the seat
// animation), DetachFromActor takes it off, and GetAttachmentPosition is the
// engine's own CM2Model::GetAttachmentPosition. Both attach methods refuse a
// cycle (M2::CanAttach): EnsureAnimated recurses through the parent chain.
// SetShadowShown only sets the flag the scene's paint reads (model/Scene.cpp,
// "ground shadows": the world's blob shadow on the actor's plane).
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
#include "cvar/Factory.h"
#include "dbc/Lookup.h"
#include "guid/Guid.h"
#include "gx/Matrix.h"
#include "item/Data.h"
#include "item/Record.h"
#include "model/DisplayInfo.h"
#include "model/M2.h"
#include "model/SceneMover.h"
#include "model/VisualKit.h"
#include "object/Resolve.h"
#include "time/Clock.h"
#include "unit/Identity.h"
#include "xml/FrameLoad.h"
#include "xml/Mixin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace Model::Scene {

namespace {

// Script indexes (kScripts order).
// OnSpellVisualImpact (PlaySpellVisual's) and OnMoveFinished (MoveTo's) are
// ClassicAPI extensions.
enum ActorScript { S_ONLOAD, S_ONUPDATE, S_ONMODELCLEARED, S_ONMODELLOADING, S_ONMODELLOADED,
                   S_ONANIMFINISHED, S_ONSPELLVISUALIMPACT, S_ONMOVEFINISHED };
const char *const kScripts[] = {"onload",         "onupdate",      "onmodelcleared",
                                "onmodelloading", "onmodelloaded", "onanimfinished",
                                "onspellvisualimpact", "onmovefinished"};
static_assert(sizeof(kScripts) / sizeof(kScripts[0]) <= Animation::kMaxScripts);

// The engine's done callback, as the character pane's (0x005062A0): a body
// track that ended (reason 0) is the actor's animation's end; another key
// bone's track stops, but for HandsClosed, which holds a weapon's grip.
void __fastcall OnAnimDone(void *model, uint32_t keyBone, uint32_t animID, uint32_t reason,
                           uint32_t /*overshoot*/, uint32_t userLow, uint32_t /*userHigh*/) {
    if (reason != 0)
        return;
    if (keyBone == 0xFFFFFFFFu || keyBone == Offsets::M2_KEY_BONE_ROOT)
        reinterpret_cast<Actor *>(static_cast<uintptr_t>(userLow))->animFinished = true;
    else if (animID != Offsets::ANIM_HANDS_CLOSED)
        M2::PlayAnimation(model, static_cast<int>(keyBone), 0xFFFFFFFFu, -1, 0, 1.0f, 1, 1);
}

// A one-shot world effect's end (EffectDoneCallback's test, reason 0): it
// hides until the kit goes.
void __fastcall OnKitWorldDone(void *model, uint32_t /*keyBone*/, uint32_t /*animID*/, uint32_t reason,
                               uint32_t /*overshoot*/, uint32_t userLow, uint32_t /*userHigh*/) {
    if (reason != 0)
        return;
    reinterpret_cast<Actor *>(static_cast<uintptr_t>(userLow))->kitWorldDone = true;
    M2::SetVisible(model, false);
}

// Takes the kit's effects off the model and undoes its tint.
void DropKitEffects(Actor &actor) {
    for (void *effect : actor.kitEffects) {
        M2::Detach(effect);
        M2::Release(effect);
    }
    actor.kitEffects.clear();
    if (actor.kitWorldEffect != nullptr) {
        M2::SetAnimDoneCallback(actor.kitWorldEffect, nullptr, 0, 0);
        ReleaseLit(actor.kitWorldEffect, actor.kitWorldLight);
        actor.kitWorldEffect = nullptr;
    }
    if (actor.model != nullptr) {
        const float white[3] = {1.0f, 1.0f, 1.0f};
        M2::SetDiffuse(actor.model, white);
        actor.ApplyAlpha();
    }
}

void ApplyKit(Actor &actor) {
    if (actor.model == nullptr || !actor.loaded || actor.spellVisualKit == 0)
        return;
    const uint8_t *kit = Model::VisualKit::Record(actor.spellVisualKit);
    if (kit == nullptr)
        return;
    Model::VisualKit::Apply(actor.SceneFrame(), actor.model, kit, actor.spellVisualKitOneShot, &actor.kitEffects);
    // The world effect, planted where the actor stands (PlantEffect
    // 0x0061FCF0). A kit given without a spell has no root aura, so it stays
    // there, as the world's does without flag 0x4000.
    const uint8_t *effect =
        DBC::Record(Offsets::VAR_SPELLVISUALEFFECTNAME_RECORDS, Offsets::VAR_SPELLVISUALEFFECTNAME_COUNT,
                    Game::Read<uint32_t>(kit, Offsets::OFF_SPELLVISUALKIT_WORLD_EFFECT));
    if (effect == nullptr)
        return;
    void *frame = actor.SceneFrame();
    void *planted =
        M2::CreateModel(Model::Pane::Scene(frame), Game::Read<const char *>(effect, Offsets::OFF_SPELLVISUALEFFECTNAME_FILE));
    if (planted == nullptr)
        return;
    actor.kitWorldEffect = planted;
    actor.kitWorldDone = false;
    std::memcpy(actor.kitWorldPosition, actor.position, sizeof actor.kitWorldPosition);
    LightAsEntity(planted, actor.kitWorldLight, frame);
    if (actor.spellVisualKitOneShot)
        M2::SetAnimDoneCallback(planted, reinterpret_cast<const void *>(&OnKitWorldDone),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&actor)), 0);
    else
        M2::SetAnimDoneCallback(planted, Model::VisualKit::LoopCallback(), 0, 0);
}

// ---- the ranged slot (Offsets: UNIT_HAND_RANGED) ----

// A wand, gun or thrown weapon is held in the right hand, a bow or crossbow
// in the left (RefreshRangedWeaponModel's ranged flag, 0x00611E31).
bool HeldRight(const Outfit::HeldLane &lane) {
    return lane.inventoryType == Offsets::INVTYPE_RANGEDRIGHT || lane.inventoryType == Offsets::INVTYPE_THROWN;
}

void DetachAt(void *model, int attachId) {
    reinterpret_cast<void(__thiscall *)(void *, int)>(Offsets::FUN_M2MODEL_DETACH_AT)(model, attachId);
}

// RefreshRangedWeaponModel's close of the old model (0x00611E41): a null
// draw callback, then its reference goes; the model stays where it hangs.
void CloseRanged(Actor &actor) {
    if (actor.rangedModel == nullptr)
        return;
    M2::SetDrawCallback(actor.rangedModel, nullptr, nullptr, true);
    M2::Release(actor.rangedModel);
    actor.rangedModel = nullptr;
}

// The ranged model off the body (hidden weapons, an emptied slot).
void TakeOffRanged(Actor &actor) {
    if (actor.rangedModel != nullptr && M2::Parent(actor.rangedModel) != nullptr)
        M2::Detach(actor.rangedModel);
    CloseRanged(actor);
}

// RefreshRangedWeaponModel (0x00611E10): the old model closed; in hand
// (`sheath` false) after both hands and the shield point are emptied, else
// at the sheath point its type has. A bow is strung (DrawBowstring).
void HangRanged(Actor &actor, bool sheath) {
    using AttachItemModel_t = int(__fastcall *)(void *model, const void *record, uint32_t bodySlot,
                                                uint32_t sheatheType, bool sheathed, bool shield, bool ranged);
    void *body = actor.model;
    if (body == nullptr)
        return;
    if (actor.ranged.itemClass == 0 || actor.rangedDisplay == nullptr || actor.hideWeapons) {
        TakeOffRanged(actor);
        return;
    }
    CloseRanged(actor);
    if (!sheath) {
        DetachAt(body, Offsets::ATTACH_HAND_RIGHT);
        DetachAt(body, Offsets::ATTACH_HAND_LEFT);
        DetachAt(body, 0);
    }
    const int at = reinterpret_cast<AttachItemModel_t>(Offsets::FUN_ATTACH_ITEM_MODEL)(
        body, actor.rangedDisplay, Offsets::BODY_SLOT_RANGED, actor.ranged.sheatheType, sheath, false,
        HeldRight(actor.ranged));
    void *weapon = at >= 0 ? M2::FindChild(body, at) : nullptr;
    if (weapon == nullptr)
        return;
    M2::AddRef(weapon);
    actor.rangedModel = weapon;
    if (actor.ranged.itemClass == Offsets::ITEM_CLASS_WEAPON &&
        actor.ranged.subclass == Offsets::ITEM_SUBCLASS_WEAPON_BOW)
        M2::SetDrawCallback(weapon, reinterpret_cast<const void *>(Offsets::FUN_DRAW_BOWSTRING), actor.stringUnit,
                            true);
}

// The ranged slot emptied: its model off, its lane, row and worn item gone.
void ClearRanged(Actor &actor) {
    TakeOffRanged(actor);
    actor.ranged = Outfit::HeldLane{};
    actor.rangedDisplay = nullptr;
    actor.worn[Offsets::INVSLOT_RANGED - 1] = Outfit::Appearance{};
}

// The unit fields DrawBowstring reads: the body model and the drawn bit.
void WriteStringUnit(Actor &actor) {
    const uint32_t flags = actor.bowDrawn ? static_cast<uint32_t>(Offsets::UNIT_STATE_BOW_DRAWN) : 0u;
    std::memcpy(actor.stringUnit + Offsets::OFF_CGOBJECT_MODEL, &actor.model, sizeof actor.model);
    std::memcpy(actor.stringUnit + Offsets::OFF_CGUNIT_STATE_FLAGS, &flags, sizeof flags);
}

// Releases the model without scripts (a destroyed actor runs none).
void ReleaseModel(Actor &actor) {
    if (actor.model == nullptr)
        return;
    CloseRanged(actor);
    Casts::Drop(actor);
    DropKitEffects(actor);
    M2::SetAnimDoneCallback(actor.model, nullptr, 0, 0);
    M2::SetEventCallback(actor.model, nullptr, 0, 0);
    M2::SetVisible(actor.model, false);
    M2::Detach(actor.model); // a rider leaves its mount
    Model::DisplayInfo::DestroyDresser(actor.dresser);
    M2::Release(actor.model);
    actor.model = nullptr;
    actor.dresser = nullptr;
    actor.dressRecord = nullptr;
    actor.displayScale = 1.0f;
    actor.displayID = 0;
    actor.areaEffect = nullptr;
    actor.areaRadius = 0.0f;
    actor.areaVisual = nullptr;
    Areas::Stop(actor);
    actor.unitGuid = 0;
    actor.lanes[0] = actor.lanes[1] = Outfit::HeldLane{};
    actor.weapons = Outfit::Weapons{};
    actor.rangedDrawn = false;
    actor.ranged = Outfit::HeldLane{};
    actor.rangedDisplay = nullptr;
    actor.ammoDisplay = 0;
    for (Outfit::Appearance &slot : actor.worn)
        slot = Outfit::Appearance{};
    actor.armorReady = true;
    actor.pendingTryOns.clear();
    actor.pendingKits.clear();
    actor.loaded = false;
    actor.animFinished = false;
    actor.kitAnimation = -1;
    actor.kitAnimationLoops = false;
    actor.kitLoopAnimation = -1;
    actor.kitAnimationEnded = false;
    actor.heldPositionMs = -1;
    actor.modelPath.clear();
}

// The model's lighting callback, `__fastcall(instance, record, user)`: the
// light of the unit's place in the scene's WMO (World::ActorLight), else the
// frame's.
void __fastcall OnModelLighting(void * /*instance*/, void *record, Actor *actor) {
    void *frame = actor->SceneFrame();
    World::ActorLight(WorldOf(frame), *actor->ambience, frame, record);
}

// The name's draw callback, as the unit's PlayerNameDrawCallback (0x006C7780):
// the plate update once the model is ready.
void __fastcall OnNameDraw(void *instance, void * /*lightingRecord*/, Actor *actor) {
    if (M2::IsReady(instance, 0, 0))
        actor->DrawName();
}

// The name's callback on the model while the actor has a name (list0 0, as
// the unit's at 0x006C7756).
void InstallName(Actor &actor) {
    if (actor.model != nullptr)
        M2::SetDrawCallback(actor.model, actor.nameText.empty() ? nullptr : reinterpret_cast<const void *>(&OnNameDraw),
                            actor.nameText.empty() ? nullptr : &actor, false);
}

} // namespace

Actor::Actor(void *scene, const char *name) : Object(s_class, scene, name), ambience(World::NewAmbience()) {}

Actor::~Actor() {
    ReleaseModel(*this);
    Areas::Drop(*this);
    Chains::Forget(*this);
    DropNameString();
    World::FreeAmbience(ambience);
}

void Actor::SetModel(void *newModel, const char *path, const uint8_t *newDressRecord) {
    ClearModel();
    if (newModel == nullptr)
        return;
    model = newModel;
    modelPath = path != nullptr ? path : "";
    dressRecord = newDressRecord;
    const auto user = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    M2::SetAnimDoneCallback(model, reinterpret_cast<const void *>(&OnAnimDone), user, 0);
    M2::SetEventCallback(model, reinterpret_cast<const void *>(&Events::OnModelEvent), user, 0);
    M2::SetRenderCallback(model, reinterpret_cast<const void *>(&OnModelLighting), this);
    ApplyAlpha();
    ApplyHighlight();
    // The new model sizes the name again.
    nameDirty = true;
    InstallName(*this);
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
    Outfit::ReadWorn(unit, guid, weapons, worn);
    Outfit::ReadRanged(unit, guid, ranged, rangedDisplay, worn[Offsets::INVSLOT_RANGED - 1]);
    const auto closeHand = reinterpret_cast<CloseHand_t>(Offsets::FUN_CLOSE_HAND);
    if (M2::FindChild(copy, 2) != nullptr)
        closeHand(copy, 1);
    if (M2::FindChild(copy, 1) != nullptr)
        closeHand(copy, 0);
    if (autoDress) {
        Outfit::HangWeapons(copy, lanes, weapons, sheathed, hideWeapons);
        TookLane(copy, 0);
        TookLane(copy, 1);
    } else {
        Outfit::Undress(copy, dresser, lanes, true);
        Outfit::TrimWorn(dresser, lanes, worn);
        ranged = Outfit::HeldLane{};
        rangedDisplay = nullptr;
        worn[Offsets::INVSLOT_RANGED - 1] = Outfit::Appearance{};
    }
    // SetModel keeps the unit fields: there is no model to clear.
    SetModel(copy, nullptr, nullptr);
    displayID = Game::Read<uint32_t>(descriptor, Offsets::OFF_UNIT_FIELD_DISPLAYID);
    Model::DisplayInfo::LightChildren(SceneFrame(), copy);
    return true;
}

void Actor::ApplyWeapons() {
    if (model == nullptr)
        return;
    Outfit::HangWeapons(model, lanes, weapons, MeleeSheathed(), hideWeapons);
    if (rangedDrawn)
        HangRanged(*this, false);
    else if (hideWeapons)
        TakeOffRanged(*this);
    Model::DisplayInfo::LightChildren(SceneFrame(), model);
}

// Sheathe state 2, as SetSheatheState (0x00611CF0) applies it at once
// (ApplySheatheChange 0x00611770: the melee items to their sheath points,
// then RefreshRangedWeaponModel(0)); a second ranged spell finds it set and
// changes nothing.
void Actor::DrawRanged() {
    if (rangedDrawn || model == nullptr)
        return;
    rangedDrawn = true;
    ApplyWeapons();
}

// ApplySheatheChange (0x00611770) leaving state 2: for 0 the ammo point and
// the weapon's hand are emptied and the hand opens; for 1 the weapon goes to
// its sheath point, if its type has one (RefreshRangedWeaponModel(1)).
void Actor::SheatheRanged() {
    using OpenHand_t = void(__fastcall *)(void *model, uint32_t hand);
    if (!rangedDrawn)
        return;
    rangedDrawn = false;
    if (model == nullptr)
        return;
    if (!sheathed) {
        HangRanged(*this, true);
        return;
    }
    DetachAt(model, Offsets::ATTACH_BOW_RELEASE);
    CloseRanged(*this);
    if (ranged.itemClass == 0)
        return;
    const bool right = HeldRight(ranged);
    DetachAt(model, right ? Offsets::ATTACH_HAND_RIGHT : Offsets::ATTACH_HAND_LEFT);
    reinterpret_cast<OpenHand_t>(Offsets::FUN_OPEN_HAND)(model, right ? 0 : 1);
}

void Actor::TookLane(void *onModel, int i) {
    ownSheathe[i] = lanes[i].sheatheType;
    ApplySheatheCategory(onModel, i);
}

void Actor::ApplySheatheCategory(void *onModel, int i) {
    if (onModel == nullptr || lanes[i].itemClass == 0)
        return;
    const uint32_t category =
        useUnitSheatheCategory ? Outfit::kSheatheDefault : sheatheCategory[i];
    const uint8_t type = Outfit::SheatheTypeFor(ownSheathe[i], category);
    if (type == lanes[i].sheatheType)
        return;
    Outfit::SetSheatheType(onModel, lanes, i, weapons, type, MeleeSheathed(), hideWeapons);
    // The re-hang empties the lane's hand, which may hold the drawn weapon.
    if (rangedDrawn && onModel == model)
        HangRanged(*this, false);
}

namespace {

bool IsAmmo(const uint8_t *record) {
    return Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) == Offsets::INVTYPE_AMMO;
}

// The worker's answer for `record` on the actor: what TryOnItem's gates allow.
// Ammo, which the worker refuses, is the actor's to shoot (an extension).
int TryOnReason(const Actor &actor, const uint8_t *record) {
    if (actor.model == nullptr ||
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID) == 0)
        return TryOn::kNotEquippable;
    if (IsAmmo(record))
        return TryOn::kSuccess;
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

// The worker on the actor's component and lanes. The slot it fills wears the
// item, and on a hand the enchant whose glow it shows; an item it evicts
// wears nothing.
void PutOn(Actor &actor, const TryOn::Request &request, const uint8_t *record) {
    if (IsAmmo(record)) {
        actor.ammoDisplay = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
        return;
    }
    if (!request.quiet)
        ShowIfNotEquippable(record);
    // Every ranged weapon takes the ranged slot, not a hand.
    const auto type = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    if (type == Offsets::INVTYPE_RANGED || type == Offsets::INVTYPE_RANGEDRIGHT || type == Offsets::INVTYPE_THROWN) {
        actor.ranged = Outfit::LaneOf(record);
        actor.rangedDisplay =
            DBC::Record(Offsets::VAR_ITEMDISPLAYINFO_RECORDS, Offsets::VAR_ITEMDISPLAYINFO_COUNT,
                        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID));
        actor.worn[Offsets::INVSLOT_RANGED - 1] = {TryOn::WornItem(request.itemID), 0};
        if (actor.rangedDrawn || actor.rangedModel != nullptr)
            HangRanged(actor, !actor.rangedDrawn);
        Model::DisplayInfo::LightChildren(actor.SceneFrame(), actor.model);
        return;
    }
    Outfit::HeldTryOn held;
    held.bodySlot = request.bodySlot;
    // The dressing room's test (TryOnItem asks the active player's skill only
    // when it shows that player) is for a unit's model. A creature display
    // or a model file holds two one-handers, as the world draws an NPC's.
    held.dualWield = actor.unitGuid == 0 || Outfit::CanDualWield(actor.unitGuid);
    held.sheathed = actor.MeleeSheathed();
    held.hide = actor.hideWeapons;
    held.enchant = request.enchant;
    const int invSlot = Outfit::TryOn(actor.model, actor.dresser, actor.armorReady, actor.lanes,
                                      actor.weapons, record, held);
    if (invSlot == 0)
        return;
    Outfit::Appearance &slot = actor.worn[invSlot - 1];
    slot.item = TryOn::WornItem(request.itemID);
    slot.enchant = 0;
    if (invSlot == Offsets::INVSLOT_MAINHAND || invSlot == Offsets::INVSLOT_OFFHAND) {
        const int lane = invSlot == Offsets::INVSLOT_OFFHAND ? 1 : 0;
        if (actor.weapons.visual[lane] != 0)
            slot.enchant = request.enchant;
        actor.TookLane(actor.model, lane);
        // The new item's hang empties its hand, which may hold the drawn
        // weapon.
        if (actor.rangedDrawn)
            HangRanged(actor, false);
    }
    Outfit::TrimWorn(actor.dresser, actor.lanes, actor.worn);
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
    else if (!request.quiet)
        ShowIfNotEquippable(record);
    return reason;
}

void Actor::ClearModel() {
    if (model == nullptr)
        return;
    ReleaseModel(*this);
    RunScript(S_ONMODELCLEARED, 0);
}

// An actor whose own animation is a death animation is dead to its spells, as
// a unit without health is to its PlayAnimation (0x005FE2F0): a kit's
// animation (a cast, an impact, a wound) does not play over the death, and a
// death drops the one that plays.
void Actor::PlayAnimation() {
    if (model == nullptr)
        return;
    if (kitAnimation >= 0 && M2::IsDeathAnimation(static_cast<int>(animation)) && !M2::IsDeathAnimation(kitAnimation)) {
        kitAnimation = -1;
        kitAnimationLoops = false;
        kitLoopAnimation = -1;
        kitAnimationEnded = true;
    }
    heldPositionMs = -1; // a paused actor holds the new animation's first frame
    // An end the scene's advance reported for the animation this replaces is
    // not this one's: the engine reports a replaced play as reason 1, which
    // OnAnimDone drops, and a pending end is dropped the same way. Two actors
    // whose animations end in one advance otherwise each see the other's
    // script replace theirs, then run OnAnimFinished for the new one.
    animFinished = false;
    if (kitAnimation >= 0) {
        M2::PlayAnimation(model, -1, static_cast<uint32_t>(kitAnimation), -1, 0, 1.0f,
                          static_cast<int>(blendOperation), 1);
        return;
    }
    const int offsetMs = static_cast<int>(animOffsetSec * 1000.0f);
    M2::PlayAnimation(model, -1, animation, variation, offsetMs, animSpeed,
                      static_cast<int>(blendOperation), 1);
}

void Actor::PlayKitAnimation(int anim, bool loops) {
    if (loops)
        kitLoopAnimation = anim;
    kitAnimation = anim;
    kitAnimationLoops = loops;
    PlayAnimation();
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

// The movement manager's tick for the actor's body, against the scene's WMO,
// then the unit's step (CGUnit_C::OnMoverStepped 0x005FAC70): the liquid
// over the feet, as the unit's liquid query finds it, and the swim decision.
// The client steps only a mover on the manager's list (moving, falling or
// with a queued command); an idle body here keeps its depth from tick to
// tick, so deciding again changes nothing.
void Advance(Actor &actor) {
    Mover::Body &body = *actor.mover;
    World::Backdrop *world = WorldOf(actor.SceneFrame());
    body.facets.context = world;
    if (!Mover::Tick(body, ClockMs(actor.SceneFrame())))
        return;
    uint32_t type = 0;
    float surface = 0.0f;
    const bool inLiquid = world != nullptr && World::LiquidAt(*world, body.position, type, surface);
    const float depth = inLiquid ? static_cast<float>(static_cast<double>(surface) - body.position[2]) : 0.0f;
    const bool swam = (body.flags & Mover::FLAG_SWIMMING) != 0;
    Mover::UpdateSwimState(body, inLiquid, depth);
    // The client's swim decision queues EnterSwim or StopSwimming, whose
    // messages the unit animates.
    const bool swims = (body.flags & Mover::FLAG_SWIMMING) != 0;
    if (swims != swam)
        Locomotion::OnMove(actor, swims ? Locomotion::Move::StartSwim : Locomotion::Move::StopSwim);
}

// ---- the body's facing (CGUnit_C::Update 0x00607ED0) ---------------------------------
//
// A unit draws its model at a body facing that follows the display facing:
// toward a side angle while strafing, at once while moving straight, and,
// standing, only past a quarter turn, the spine's two twist bones turning the
// shoulders and head through the rest. The scene's actor is the active
// mover, whose display facing is the mover's own; the world's camera sets it
// at the render (MakeViewAngles 0x0050F7A1 calls 0x00600CD0), after the
// units' update, so the body pass reads the one the last frame set. The
// pass's shuffle (11, 12) is Locomotion::OnBodyTurn's, given the turn it made.

const Game::Live<float> kHalfPi{Offsets::VAR_HALF_PI};
const Game::Live<float> kQuarterPi{Offsets::VAR_QUARTER_PI};
const Game::Live<float> kFacingEpsilon{Offsets::VAR_MOVER_FACING_EPSILON};
const Game::Live<float> kMsToSec{Offsets::VAR_ONE_THOUSANDTH};
constexpr uint32_t kTurning = 0x1, kTwistLow = 0x80, kTwistHigh = 0x100;

// WrapAngleToPi (0x006084F0).
double WrapPi(float a) {
    return reinterpret_cast<double(__stdcall *)(float)>(Offsets::FUN_WRAP_ANGLE_TO_PI)(a);
}

void SetTwist(void *model, int bone, uint32_t flags, float angle) {
    using Flags_t = void(__thiscall *)(void *model, int bone, uint32_t flags, uint32_t mask);
    using Override_t = void(__thiscall *)(void *model, int bone, const float *m);
    using Rotation_t = float *(__fastcall *)(float *out, const float *axis, float angle, int unitAxis);
    reinterpret_cast<Flags_t>(Offsets::FUN_M2MODEL_SET_KEY_BONE_FLAGS)(model, bone, flags,
                                                                      Offsets::M2_KEY_BONE_OVERRIDE);
    if (flags == 0)
        return;
    const float up[3] = {0.0f, 0.0f, 1.0f};
    float m[16];
    reinterpret_cast<Override_t>(Offsets::FUN_M2MODEL_SET_KEY_BONE_OVERRIDE)(
        model, bone, reinterpret_cast<Rotation_t>(Offsets::FUN_MATRIX_AXIS_ROTATION)(m, up, angle, 1));
}

void ClearTwist(void *model) {
    SetTwist(model, Offsets::M2_KEY_BONE_TWIST_LOW, 0, 0.0f);
    SetTwist(model, Offsets::M2_KEY_BONE_TWIST_HIGH, 0, 0.0f);
}

// The model's twist bones, marked once it has loaded (OnModelLoaded
// 0x0060CE70).
void MarkTwistBones(Actor &actor) {
    using Has_t = int(__thiscall *)(void *model, int bone);
    Actor::Facings &f = actor.facings;
    if (actor.model == f.bonesOf || !actor.loaded)
        return;
    const auto has = reinterpret_cast<Has_t>(Offsets::FUN_M2MODEL_HAS_KEY_BONE);
    f.bits &= ~(kTwistLow | kTwistHigh);
    if (has(actor.model, Offsets::M2_KEY_BONE_TWIST_LOW) != 0)
        f.bits |= kTwistLow;
    if (has(actor.model, Offsets::M2_KEY_BONE_TWIST_HIGH) != 0)
        f.bits |= kTwistHigh;
    f.bonesOf = actor.model;
}

// The body pass (0x00608075 to 0x006083E6) for a live unit; returns the turn
// it made past the strafe aim and the follow (the function's argument slot,
// which the tail reads).
float TurnBody(Actor &actor) {
    using D = double;
    const Mover::Body &b = *actor.mover;
    Actor::Facings &f = actor.facings;
    if (b.flags & Mover::FLAG_SWIMMING) {
        f.body = f.display;
    } else if (b.flags & 0xC) {
        // Strafing aims a quarter turn aside, an eighth when also moving
        // forward or back, to the left for left strafe forward and right
        // strafe back; a quarter of the gap each frame.
        float aside = (b.flags & 0x3) ? kQuarterPi : kHalfPi;
        if ((((b.flags >> 1) & 0x2) ^ (b.flags & 0x2)) == 0)
            aside = -aside;
        const double step = WrapPi(static_cast<float>((D(f.display) - f.body) + aside)) * 0.25;
        f.body = static_cast<float>(WrapPi(static_cast<float>(step + f.body)));
    } else if (b.flags & 0x2003) {
        f.body = f.display;
    }
    // The bones are set once the model has loaded (before it, each call
    // would queue on the model, and it has no twist bones marked).
    void *model = actor.loaded ? actor.model : nullptr;
    const float gap = static_cast<float>(WrapPi(static_cast<float>(D(f.display) - f.body)));
    const float absGap = std::fabs(gap);
    if (absGap < kFacingEpsilon) {
        if (model != nullptr)
            ClearTwist(model);
        return 0.0f;
    }
    // Past a quarter turn the body follows at once; standing still, not
    // turning, it also catches up at eight times the turn rate.
    float turn = 0.0f;
    if (absGap > kHalfPi)
        turn = std::copysign(static_cast<float>(D(absGap) - kHalfPi), gap);
    if (!(b.flags & 0xC) && !(f.bits & kTurning)) {
        const uint32_t ms = ClockMs(actor.SceneFrame()) - f.stamp;
        double catchUp = ((D(ms) * kMsToSec) * b.turnRate) * 8.0;
        if (catchUp > absGap)
            catchUp = absGap;
        turn = static_cast<float>(D(std::copysign(static_cast<float>(catchUp), gap)) + turn);
    }
    const double body = WrapPi(static_cast<float>(D(turn) + f.body));
    f.body = static_cast<float>(body);
    const double rest = std::fabs(WrapPi(static_cast<float>(D(f.display) - body)));
    if (model == nullptr)
        return turn;
    if (rest < kFacingEpsilon) {
        ClearTwist(model);
        return turn;
    }
    // The twist: the lower bone half the rest, the upper what remains, an
    // eighth of a turn each at most. A rider's lower bone is its mount's.
    const float restF = static_cast<float>(rest);
    double left = rest;
    if (M2::Parent(model) == nullptr && (f.bits & kTwistLow)) {
        float low = static_cast<float>(D(restF) * 0.5);
        if (low > kQuarterPi)
            low = kQuarterPi;
        SetTwist(model, Offsets::M2_KEY_BONE_TWIST_LOW, Offsets::M2_KEY_BONE_OVERRIDE, std::copysign(low, gap));
        left = D(restF) - low;
    }
    if (f.bits & kTwistHigh) {
        if (!(left < kQuarterPi))
            left = kQuarterPi;
        SetTwist(model, Offsets::M2_KEY_BONE_TWIST_HIGH, Offsets::M2_KEY_BONE_OVERRIDE,
                 std::copysign(static_cast<float>(left), gap));
    }
    return turn;
}

// The active mover's display facing (0x00600CD0): the mover's own, and the
// time it last stood still.
void FaceDisplay(Actor &actor) {
    Actor::Facings &f = actor.facings;
    f.display = actor.mover->facing;
    if (actor.mover->flags & 0x30) {
        f.bits |= kTurning;
    } else {
        f.bits &= ~kTurning;
        f.stamp = ClockMs(actor.SceneFrame());
    }
}

// The facings of a body put down (0x00601020 and the unit's seed): both the
// mover's, standing still.
void ResetFacings(Actor &actor) {
    Actor::Facings &f = actor.facings;
    f.display = f.body = actor.mover->facing;
    f.bits &= ~kTurning;
    f.stamp = ClockMs(actor.SceneFrame());
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
    float shownAlpha = alpha * frameAlpha;
    // The fade pass (0x00672EF0): x = (1 - age x 0.0005) x the alpha, held to
    // 0..1, shown as (3 - 2x)x^2.
    if (fadingOut) {
        const uint32_t age = Time::Clock::Elapsed(fadeStartMs, ClockMs(SceneFrame()));
        double x = (1.0 - static_cast<double>(age) * Game::Read<float>(Offsets::VAR_MODEL_FADE_RATE)) * shownAlpha;
        x = x <= 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
        shownAlpha = static_cast<float>((Game::Read<float>(Offsets::VAR_THREE) - (x + x)) * x * x);
    }
    M2::SetAlpha(model, shownAlpha);
}

// SetLight's colour (0x00614550) while a bit is set, else ClearLight's black
// (0x006144F0); a new model takes the bits' colour (a unit's copy may carry
// its own target glow).
// GetNamePosition (0x00608640): a mounted unit's model is its mount's, which
// places the name at its PlayerNameMounted attachment; else the model's
// PlayerName; without the attachment, 1.25 times the scaled box height above
// the feet. A seated rider (AttachToMount, AttachToActor) is the mounted
// unit here: its parent's model is the one asked.
bool Actor::NamePosition(float (&out)[3]) const {
    if (model == nullptr || !loaded)
        return false;
    void *mount = M2::Parent(model);
    void *body = mount != nullptr ? mount : model;
    const uint32_t id = mount != nullptr ? static_cast<uint32_t>(Offsets::M2_ATTACHMENT_PLAYER_NAME_MOUNTED)
                                         : static_cast<uint32_t>(Offsets::M2_ATTACHMENT_PLAYER_NAME);
    if (M2::HasAttachment(body, id)) {
        M2::GetAttachmentPosition(body, id, out);
        return true;
    }
    float m[16];
    M2::GetBaseTransform(body, m);
    float box[6];
    M2::GetBoundingBox(body, box);
    const float scale = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    out[0] = m[12];
    out[1] = m[13];
    out[2] = scale * (box[5] - box[2]) * Game::Read<float>(Offsets::VAR_NAME_HEIGHT_FACTOR) + m[14];
    return true;
}

void Actor::DropNameString() {
    // A string of a font the world has destroyed went with it.
    if (nameString != nullptr && nameFont == Game::Read<void *>(Offsets::VAR_UNIT_NAME_FONT))
        reinterpret_cast<void(__thiscall *)(void **)>(Offsets::FUN_GXU_FONT_DESTROY_STRING)(&nameString);
    nameString = nullptr;
    nameFont = nullptr;
}

// UpdatePlate's (0x006C6E90) name: the plate scale from the name position's
// height over the feet, the text made again while dirty, then added to the
// world's name batch at the name position raised by the block height, and
// the batch drawn. A custom draw runs under the scene's view with no offset
// (0x0070D96D), and the paint gives the scene its eye as the origin, so the
// position goes in absolute, as the world's. The world's name font and batch
// exist only in the world: at the glue screens nothing draws.
void Actor::DrawName() {
    using CreateString_t = int(__fastcall *)(void *font, const char *text, float size, const float *pos,
                                             float width, float height, float spacing, void **out, int vjust,
                                             int hjust, uint32_t flags, const uint32_t *colour, float extra);
    using Expand_t = const char *(__fastcall *)(const char *text);
    void *font = Game::Read<void *>(Offsets::VAR_UNIT_NAME_FONT);
    void *batch = Game::Read<void *>(Offsets::VAR_UNIT_NAME_BATCH);
    if (nameString != nullptr && nameFont != font) {
        nameString = nullptr;
        nameDirty = true;
    }
    float at[3];
    if (font == nullptr || batch == nullptr || nameText.empty() || !NamePosition(at))
        return;
    float m[16];
    M2::GetBaseTransform(M2::Parent(model) != nullptr ? M2::Parent(model) : model, m);
    const float d = at[2] - m[14];
    const uint32_t scaleBits = Offsets::UNIT_NAME_SCALE;
    float scale;
    std::memcpy(&scale, &scaleBits, sizeof scale);
    const float high = Game::Read<float>(Offsets::VAR_UNIT_NAME_SCALE_HEIGHT);
    if (d > high)
        scale = d / high * Game::Read<float>(Offsets::VAR_UNIT_NAME_SCALE_GROWTH) *
                Game::Read<float>(Offsets::VAR_UNIT_NAME_SCALE_BASE);
    if (nameString == nullptr || nameDirty) {
        DropNameString();
        const auto lines = std::count(nameText.begin(), nameText.end(), '\n');
        nameBlockHeight = static_cast<float>(lines) * scale;
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        const char *text = reinterpret_cast<Expand_t>(Offsets::FUN_EXPAND_GRAMMAR_ESCAPES)(nameText.c_str());
        reinterpret_cast<CreateString_t>(Offsets::FUN_TEXT_CREATE_STRING)(
            font, text, scale, origin, 100000.0f, 100000.0f, 0.0f, &nameString, 2, 1,
            Offsets::UNIT_NAME_STRING_FLAGS, &nameColor, 0.0f);
        nameFont = font;
        nameDirty = false;
    }
    if (nameString == nullptr)
        return;
    reinterpret_cast<int(__fastcall *)(void *, void *)>(Offsets::FUN_GXU_FONT_BATCH_ADD)(batch, nameString);
    const float placed[3] = {at[0], at[1], at[2] + nameBlockHeight};
    reinterpret_cast<void(__fastcall *)(void *, const float *)>(Offsets::FUN_GXU_STRING_SET_POSITION)(nameString,
                                                                                                      placed);
    reinterpret_cast<void(__thiscall *)(void *)>(Offsets::FUN_GXU_FONT_BATCH_DRAW)(batch);
}

void Actor::ApplyHighlight() {
    if (model == nullptr)
        return;
    float rgb[3] = {0.0f, 0.0f, 0.0f};
    if (highlightBits != 0)
        std::memcpy(rgb, LightAmbient(SceneFrame()), sizeof rgb);
    reinterpret_cast<void(__thiscall *)(void *, const float *)>(Offsets::FUN_M2MODEL_SET_EMISSIVE)(model, rgb);
}

// A dynamic object's loaded hook (slot 13, 0x005D52F0): the model is scaled
// to the area's radius over its bounding-sphere radius, or over the effect
// row's size when it has no sphere, and a model with animation 158 and none
// for 0 loops 158 (the done callback 0x005D5580 plays it again, as the
// actor's own animation replays).
void SizeSpellArea(Actor &actor) {
    float sphere[4];
    M2::GetBoundingSphere(actor.model, sphere);
    if (sphere[3] > Game::Read<double>(Offsets::VAR_DYNAMIC_OBJECT_MIN_SPHERE)) {
        actor.scale = actor.areaRadius / sphere[3];
    } else {
        const auto size = Game::Read<float>(actor.areaEffect, Offsets::OFF_SPELLVISUALEFFECTNAME_AREA_SIZE);
        if (size > 0.0f)
            actor.scale = actor.areaRadius / size;
    }
    if (M2::HasAnimation(actor.model, Offsets::ANIM_HOLD) && !M2::HasAnimation(actor.model, 0)) {
        actor.animation = Offsets::ANIM_HOLD;
        actor.PlayAnimation();
    }
}

void Actor::Update(float elapsedSec, float sceneSec) {
    if (mover != nullptr) {
        Advance(*this);
        std::memcpy(position, mover->position, sizeof position);
        yaw = mover->facing;
        pitch = Mover::ModelPitch(*mover);
        MarkTwistBones(*this);
        Locomotion::OnBodyTurn(*this, TurnBody(*this));
        FaceDisplay(*this);
    }
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
            for (const auto &[kitID, oneShot] : pendingKits)
                if (const uint8_t *kit = Model::VisualKit::Record(kitID))
                    Model::VisualKit::Apply(SceneFrame(), model, kit, oneShot);
            pendingKits.clear();
            // The pick an unloaded model could not take (PlayAnimation keeps
            // it pending at +0xD64).
            Locomotion::Repick(*this);
            if (areaEffect != nullptr)
                SizeSpellArea(*this);
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
        if (kitAnimation >= 0) {
            // A kit's animation is the spell's, not the script's: no
            // OnAnimFinished. A ranged load goes on to its hold, as the
            // unit's does; else a once-only one gives back the precast's
            // loop, or the actor's own.
            if (const int next = Casts::RangedAnimationAfter(*this, kitAnimation); next >= 0) {
                kitAnimation = next;
                if (kitAnimationLoops)
                    kitLoopAnimation = next;
            } else if (!kitAnimationLoops) {
                kitAnimation = kitLoopAnimation;
                kitAnimationLoops = kitLoopAnimation >= 0;
                kitAnimationEnded = true;
            }
            PlayAnimation();
        } else {
            if (!Locomotion::FollowOn(*this))
                PlayAnimation();
            RunScript(S_ONANIMFINISHED, 0);
        }
    }
    // A fade out shows its step each frame, and the actor hides once its age
    // passes the fade's time, where the world releases the model.
    if (fadingOut) {
        if (Time::Clock::Elapsed(fadeStartMs, ClockMs(SceneFrame())) > Offsets::MODEL_FADE_OUT_MS) {
            fadingOut = false;
            shown = false;
            if (model != nullptr)
                M2::SetVisible(model, false);
        }
        ApplyAlpha();
    }
    WriteStringUnit(*this);
    Casts::Update(*this, sceneSec);
    Areas::Update(*this, sceneSec);
    if (kitWorldEffect != nullptr)
        SettleLight(kitWorldEffect, kitWorldLight.get(), sceneSec);
    // The casts that reached their target, with (spellID, target, castID).
    // Taken first: a script may cast again, and its impact waits for the next
    // update.
    if (!spellImpacts.empty()) {
        const std::vector<SpellImpact> impacts = std::move(spellImpacts);
        spellImpacts.clear();
        void *L = Game::Lua::State();
        for (const SpellImpact &impact : impacts) {
            if (L == nullptr || !HasScript(S_ONSPELLVISUALIMPACT))
                break;
            Game::Lua::PushNumber(L, impact.spellID);
            Animation::Push(L, impact.target);
            Game::Lua::PushNumber(L, impact.castID);
            RunScript(S_ONSPELLVISUALIMPACT, 3);
        }
    }
    // The moves the mover reported done. Taken first: a script may move the
    // actor again.
    for (uint32_t finished = std::exchange(movesFinished, 0); finished != 0; --finished)
        RunScript(S_ONMOVEFINISHED, 0);
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
    Casts::Link(*this);
    Areas::Link(*this);
    if (kitWorldEffect != nullptr && M2::IsReady(kitWorldEffect, 0, 1)) {
        M2::SetVisible(kitWorldEffect, shown && !kitWorldDone);
        M2::SetAnimated(kitWorldEffect, true);
    }
    if (model == nullptr || !M2::IsReady(model, 0, 1))
        return;
    if (M2::Parent(model) == nullptr) // a seated rider is animated by its mount
        M2::SetAnimated(model, true);
    M2::SetVisible(model, shown);
}

// A seated rider is placed the same way, relative to its seat: the world's
// Mount gives the attached rider its base transform too.
void Actor::Place() {
    Casts::Place(*this);
    Areas::Place(*this);
    const float axisX[3] = {1.0f, 0.0f, 0.0f};
    const float axisY[3] = {0.0f, 1.0f, 0.0f};
    const float axisZ[3] = {0.0f, 0.0f, 1.0f};
    if (kitWorldEffect != nullptr) {
        const float unit[3] = {1.0f, 1.0f, 1.0f};
        M2::SetBaseTRS(kitWorldEffect, kitWorldPosition, 0.0f, axisZ, unit);
    }
    if (model == nullptr)
        return;
    const float scales[3] = {scale, scale, scale};
    M2::SetBaseTRS(model, position, ShownYaw(), axisZ, scales);
    float *matrix = M2::BaseMatrix(model);
    if (pitch != 0.0f)
        Gx::Rotate(matrix, pitch, axisY);
    if (roll != 0.0f)
        Gx::Rotate(matrix, roll, axisX);
    if (!loaded || !(useCenterForOrigin[0] || useCenterForOrigin[1] || useCenterForOrigin[2]))
        return;
    float box[6];
    M2::GetSequenceBounds(model, animation, variation < 0 ? 0u : static_cast<uint32_t>(variation),
                          box);
    float offset[3];
    for (int i = 0; i < 3; ++i)
        offset[i] = useCenterForOrigin[i] ? -(box[i] + box[i + 3]) * 0.5f : 0.0f;
    Gx::Translate(matrix, offset);
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
    if (model == nullptr)
        return false;
    actor.displayID = displayID;
    actor.displayScale = Game::Read<float>(
        DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                    Offsets::VAR_CREATUREDISPLAYINFO_COUNT, displayID),
        Offsets::OFF_CREATUREDISPLAYINFO_SCALE);
    return true;
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
    // A WMO is not an M2; the scene shows one (SetWorldModel).
    void *model = World::IsWmoPath(path) ? nullptr : M2::CreateModel(Model::Pane::Scene(actor->SceneFrame()), path);
    actor->SetModel(model, path, nullptr);
    Game::Lua::PushBool(L, model != nullptr);
    return 1;
}

// A spell's ground area, as the world shows the dynamic object a spell like
// Blizzard leaves: the model the dynamic object names (slot 10, 0x005D5890 ->
// 0x005D57C0: the spell's visual must have the area flag +0x2C, and its +0x30
// SpellVisualEffectName row gives the file), sized to `radius` once loaded.
int __fastcall Script_SetModelBySpellArea(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (Game::Lua::IsNumber(L, 2) == 0 || Game::Lua::IsNumber(L, 3) == 0 || !(FloatArg(L, 3) > 0.0f)) {
        Game::Lua::Error(L, "Usage: %s:SetModelBySpellArea(spellID, radius)", actor->DisplayName());
        return 0;
    }
    using GetVisual_t = const uint8_t *(__fastcall *)(const uint8_t *spell);
    const auto *spell = DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT,
                                    static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)));
    const uint8_t *visual = spell != nullptr ? reinterpret_cast<GetVisual_t>(Offsets::FUN_GET_SPELL_VISUAL)(spell) : nullptr;
    const uint8_t *effect =
        visual != nullptr && Game::Read<uint32_t>(visual, Offsets::OFF_SPELLVISUAL_HAS_AREA) != 0
            ? DBC::Record(Offsets::VAR_SPELLVISUALEFFECTNAME_RECORDS, Offsets::VAR_SPELLVISUALEFFECTNAME_COUNT,
                          Game::Read<uint32_t>(visual, Offsets::OFF_SPELLVISUAL_IMPACT_MODEL))
            : nullptr;
    const char *path = effect != nullptr ? Game::Read<const char *>(effect, Offsets::OFF_SPELLVISUALEFFECTNAME_FILE) : nullptr;
    void *model = path != nullptr && *path != '\0' ? M2::CreateModel(Model::Pane::Scene(actor->SceneFrame()), path) : nullptr;
    actor->SetModel(model, path, nullptr);
    if (model != nullptr) {
        actor->areaEffect = effect;
        actor->areaRadius = FloatArg(L, 3);
        actor->areaVisual = visual;
    }
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
    Outfit::TrimWorn(actor->dresser, actor->lanes, actor->worn);
    if (weapons)
        ClearRanged(*actor);
    return 0;
}

int __fastcall Script_UndressSlot(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "UndressSlot(inventorySlot)"))
        return 0;
    const int invSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (invSlot == Offsets::INVSLOT_RANGED) {
        ClearRanged(*actor);
        return 0;
    }
    Outfit::UndressSlot(actor->model, actor->dresser, actor->lanes, invSlot);
    Outfit::TrimWorn(actor->dresser, actor->lanes, actor->worn);
    return 0;
}

int __fastcall Script_DressPlayerSlot(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "DressPlayerSlot(invSlot)"))
        return 0;
    void *unit = actor->unitGuid != 0 ? ::Object::ByGuid(Offsets::TYPEMASK_UNIT, actor->unitGuid)
                                      : nullptr;
    const int invSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (invSlot == Offsets::INVSLOT_RANGED) {
        if (actor->model == nullptr || unit == nullptr)
            return 0;
        TakeOffRanged(*actor);
        Outfit::ReadRanged(unit, actor->unitGuid, actor->ranged, actor->rangedDisplay,
                           actor->worn[Offsets::INVSLOT_RANGED - 1]);
        if (actor->rangedDrawn)
            HangRanged(*actor, false);
        Model::DisplayInfo::LightChildren(actor->SceneFrame(), actor->model);
        return 0;
    }
    Outfit::DressSlot(actor->model, actor->dresser, actor->armorReady, actor->lanes,
                      actor->weapons, unit, actor->unitGuid, invSlot, actor->MeleeSheathed(),
                      actor->hideWeapons);
    if (actor->model == nullptr || unit == nullptr)
        return 0;
    if (invSlot == Offsets::INVSLOT_MAINHAND || invSlot == Offsets::INVSLOT_OFFHAND) {
        actor->TookLane(actor->model, invSlot == Offsets::INVSLOT_OFFHAND ? 1 : 0);
        if (actor->rangedDrawn)
            HangRanged(*actor, false);
    }
    // The slot wears what the unit wears there; the hand's row and glow are
    // the unit's now.
    if (invSlot >= 1 && invSlot <= Outfit::kInventorySlots) {
        Outfit::Worn unitWorn;
        Outfit::ReadWorn(unit, actor->unitGuid, actor->weapons, unitWorn);
        actor->worn[invSlot - 1] = unitWorn[invSlot - 1];
    }
    Outfit::TrimWorn(actor->dresser, actor->lanes, actor->worn);
    Model::DisplayInfo::LightChildren(actor->SceneFrame(), actor->model);
    return 0;
}

int __fastcall Script_GetItemTransmogInfo(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "GetItemTransmogInfo(inventorySlots)"))
        return 0;
    const int invSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (actor->model == nullptr || invSlot < 1 || invSlot > Outfit::kInventorySlots)
        return 0;
    TryOn::PushTransmogInfo(L, actor->worn[invSlot - 1]);
    return 1;
}

int __fastcall Script_GetItemTransmogInfoList(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    TryOn::PushTransmogInfoList(L, actor->worn);
    return 1;
}

// TryOn with the info's item and enchant; `inventorySlots` names the hand a
// weapon goes in. No appearance changes nothing. The caller shows the error.
int __fastcall Script_SetItemTransmogInfo(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    TryOn::Request request;
    if (!TryOn::ReadTransmogInfo(L, actor->DisplayName(), request))
        return 0;
    Game::Lua::PushNumber(L, request.itemID == 0 ? TryOn::kNotEquippable
                                                 : actor->TryOnItem(request));
    return 1;
}

// The slots TryOnReason's arms accept: a hand or the ranged slot on any
// model, armor on one that takes it.
bool SlotAllowed(const Actor &actor, int invSlot) {
    return actor.model != nullptr &&
           (invSlot == Offsets::INVSLOT_RANGED ||
            Outfit::SlotAllowed(invSlot, (actor.dresser != nullptr || actor.dressRecord != nullptr) &&
                                             actor.armorReady));
}

int __fastcall Script_IsSlotAllowed(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "IsSlotAllowed(inventorySlots)"))
        return 0;
    Game::Lua::PushBool(L, SlotAllowed(*actor, static_cast<int>(Game::Lua::ToNumber(L, 2))));
    return 1;
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
    actor->SheatheRanged();
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

// A hand's sheath point while sheathed. The ranged slot has no category (a
// 1.12 ranged weapon hangs in hand), so other slots change nothing.
int __fastcall Script_SetSheathedCategory(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 2, actor->DisplayName(),
                                        "SetSheathedCategory(inventorySlots, category)"))
        return 0;
    const int invSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (invSlot != Offsets::INVSLOT_MAINHAND && invSlot != Offsets::INVSLOT_OFFHAND)
        return 0;
    const int lane = invSlot == Offsets::INVSLOT_OFFHAND ? 1 : 0;
    actor->sheatheCategory[lane] = static_cast<uint32_t>(Game::Lua::ToNumber(L, 3));
    if (actor->model != nullptr) {
        actor->ApplySheatheCategory(actor->model, lane);
        Model::DisplayInfo::LightChildren(actor->SceneFrame(), actor->model);
    }
    return 0;
}

// The unit's own categories in place of SetSheathedCategory's: a 1.12 unit
// has none, so each hand takes its item's own sheath point.
int __fastcall Script_UseUnitSheatheCategory(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    actor->useUnitSheatheCategory = Game::Lua::ToBoolean(L, 2) != 0;
    if (actor->model != nullptr) {
        actor->ApplySheatheCategory(actor->model, 0);
        actor->ApplySheatheCategory(actor->model, 1);
        Model::DisplayInfo::LightChildren(actor->SceneFrame(), actor->model);
    }
    return 0;
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
    const bool seated = rider->SceneFrame() == mount->SceneFrame() &&
                        M2::CanAttach(rider->model, mount->model);
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

// The engine's GetAttachmentPosition, which reads the bones of the frame the
// scene last drew (EnsureAnimated animates only a model not yet animated in
// that frame). It waits for unloaded files and animates the parents first, so
// the model and every parent must have loaded; it does not test the id.
int __fastcall Script_GetAttachmentPosition(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr ||
        !NumberArgs(L, 2, 1, actor->DisplayName(), "GetAttachmentPosition(attachmentID)"))
        return 0;
    for (void *m = actor->model; m != nullptr; m = M2::Parent(m))
        if (!M2::IsLoaded(m, 0, 0))
            return 0;
    const uint32_t id = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    if (actor->model == nullptr || !M2::HasAttachment(actor->model, id))
        return 0;
    float point[3];
    M2::GetAttachmentPosition(actor->model, id, point);
    for (float v : point)
        Game::Lua::PushNumber(L, v);
    return 3;
}

// AttachToMount on any attachment, from the child's side, without a seat
// animation. An id the parent's model lacks hangs the child at the parent's
// origin (EnsureAnimated's unresolved leg).
int __fastcall Script_AttachToActor(void *L) {
    constexpr char kUsage[] = "AttachToActor(parent, attachmentID)";
    Actor *child = Self(L);
    if (child == nullptr)
        return 0;
    Actor *parent = static_cast<Actor *>(Animation::Resolve(L, 2, Actor::s_class));
    if (parent == nullptr) {
        Game::Lua::Error(L, "Usage: %s:%s", child->DisplayName(), kUsage);
        return 0;
    }
    if (!NumberArgs(L, 3, 1, child->DisplayName(), kUsage))
        return 0;
    const bool attached = child->SceneFrame() == parent->SceneFrame() &&
                          M2::CanAttach(child->model, parent->model);
    if (attached) {
        M2::Detach(child->model);
        M2::Attach(child->model, parent->model, static_cast<uint32_t>(Game::Lua::ToNumber(L, 3)));
    }
    Game::Lua::PushBool(L, attached);
    return 1;
}

// The cast, from this actor at the target (another actor of the scene, or
// none: the caster itself). See model/SceneSpell.cpp.
int __fastcall Script_PlaySpellVisual(void *L) {
    constexpr char kUsage[] = "PlaySpellVisual(spellID [, target [, castTimeMs]])";
    Actor *caster = Self(L);
    if (caster == nullptr || !NumberArgs(L, 2, 1, caster->DisplayName(), kUsage))
        return 0;
    Actor *target = nullptr;
    if (Game::Lua::Type(L, 3) > Game::Lua::TYPE_NIL) {
        target = static_cast<Actor *>(Animation::Resolve(L, 3, Actor::s_class));
        if (target == nullptr) {
            Game::Lua::Error(L, "Usage: %s:%s", caster->DisplayName(), kUsage);
            return 0;
        }
    }
    if (Game::Lua::Type(L, 4) > Game::Lua::TYPE_NIL && !NumberArgs(L, 4, 1, caster->DisplayName(), kUsage))
        return 0;
    const int castTimeMs =
        Game::Lua::IsNumber(L, 4) ? static_cast<int>(Game::Lua::ToNumber(L, 4)) : -1;
    const uint32_t castID =
        target == nullptr || target->SceneFrame() == caster->SceneFrame()
            ? Casts::Play(*caster, static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)), target, castTimeMs)
            : 0;
    if (castID == 0)
        return 0;
    Game::Lua::PushNumber(L, castID);
    return 1;
}

// Interrupts the actor's casts still preparing (model/SceneSpell.cpp).
int __fastcall Script_StopSpellVisual(void *L) {
    if (Actor *caster = Self(L))
        Casts::Stop(*caster);
    return 0;
}

// Retail PlayerModel's ApplySpellVisualKit on the actor: the kit joins the
// ones shown (Model::VisualKit::Apply, the model owning the effects), where
// SetSpellVisualKit replaces the actor's one kit. Applied once the model has
// loaded, as SetSpellVisualKit's.
int __fastcall Script_ApplySpellVisualKit(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "ApplySpellVisualKit(spellVisualKitID [, oneShot])"))
        return 0;
    const auto kitID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    const bool oneShot = Game::Lua::ToBoolean(L, 3) != 0;
    const uint8_t *kit = Model::VisualKit::Record(kitID);
    if (kit == nullptr || actor->model == nullptr)
        return 0;
    if (actor->loaded)
        Model::VisualKit::Apply(actor->SceneFrame(), actor->model, kit, oneShot);
    else
        actor->pendingKits.emplace_back(kitID, oneShot);
    return 0;
}

int __fastcall Script_SetPlayAnimationSounds(void *L) {
    if (Actor *actor = Self(L))
        actor->playAnimationSounds = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __fastcall Script_GetPlayAnimationSounds(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->playAnimationSounds);
    return 1;
}

// A ClassicAPI extension: PlaySound(soundKitID) at the actor, heard from the
// scene camera, as a unit's voice is heard at its distance in the world.
// Returns `willPlay, soundHandle` as PlaySound does; StopSound stops it.
int __fastcall Script_PlaySound(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), "PlaySound(soundKitID)"))
        return 0;
    void *sound = Sounds::Play(actor->SceneFrame(), static_cast<int>(Game::Lua::ToNumber(L, 2)), actor->position);
    Game::Lua::PushBool(L, sound != nullptr);
    if (sound != nullptr)
        Game::Lua::PushNumber(L, static_cast<double>(reinterpret_cast<uintptr_t>(sound)));
    else
        Game::Lua::PushNil(L);
    return 2;
}

int __fastcall Script_SetShadowShown(void *L) {
    if (Actor *actor = Self(L))
        actor->shadowShown = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __fastcall Script_IsShadowShown(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->shadowShown);
    return 1;
}

// The highlight's sources: a unit's two light bits.
bool HighlightBit(void *L, int index, uint32_t &bit) {
    const char *kind = Game::Lua::IsString(L, index) ? Game::Lua::ToString(L, index) : nullptr;
    if (kind != nullptr && std::strcmp(kind, "target") == 0)
        bit = Offsets::CGOBJECT_LIGHT_BIT_TARGET;
    else if (kind != nullptr && std::strcmp(kind, "mouseover") == 0)
        bit = Offsets::CGOBJECT_LIGHT_BIT_MOUSEOVER;
    else
        return false;
    return true;
}

// SetLight / ClearLight (0x00614550 / 0x006144F0): setting a bit gives the
// model the colour again, clearing one blacks it only when none is left.
int __fastcall Script_SetHighlight(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    uint32_t bit = 0;
    if (!HighlightBit(L, 2, bit)) {
        Game::Lua::Error(L, "Usage: %s:SetHighlight(\"target\" or \"mouseover\", highlighted)",
                         actor->DisplayName());
        return 0;
    }
    if (Game::Lua::ToBoolean(L, 3) != 0) {
        actor->highlightBits |= 1u << bit;
        actor->ApplyHighlight();
    } else {
        actor->highlightBits &= ~(1u << bit);
        if (actor->highlightBits == 0)
            actor->ApplyHighlight();
    }
    return 0;
}

int __fastcall Script_IsHighlighted(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    uint32_t mask = ~0u;
    uint32_t bit = 0;
    if (HighlightBit(L, 2, bit))
        mask = 1u << bit;
    Game::Lua::PushBool(L, (actor->highlightBits & mask) != 0);
    return 1;
}

// A colour channel as a byte, as the fog's (clamped, NaN to 1).
uint32_t ColorByte(float c) {
    if (!(c < 1.0f))
        c = 1.0f;
    else if (!(c > 0.0f))
        c = 0.0f;
    return static_cast<uint32_t>(c * 255.0f + 0.5f);
}

int __fastcall Script_SetSelectionCircle(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 3, actor->DisplayName(),
                                        "SetSelectionCircle(colorR, colorG, colorB [, a])"))
        return 0;
    const float a = Game::Lua::IsNumber(L, 5) ? static_cast<float>(Game::Lua::ToNumber(L, 5)) : 1.0f;
    actor->selectionCircle = ColorByte(a) << 24 |
                             ColorByte(static_cast<float>(Game::Lua::ToNumber(L, 2))) << 16 |
                             ColorByte(static_cast<float>(Game::Lua::ToNumber(L, 3))) << 8 |
                             ColorByte(static_cast<float>(Game::Lua::ToNumber(L, 4)));
    actor->selectionCircleShown = true;
    return 0;
}

int __fastcall Script_ClearSelectionCircle(void *L) {
    if (Actor *actor = Self(L))
        actor->selectionCircleShown = false;
    return 0;
}

int __fastcall Script_GetSelectionCircle(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !actor->selectionCircleShown)
        return 0;
    for (int shift : {16, 8, 0, 24})
        Game::Lua::PushNumber(L, ((actor->selectionCircle >> shift) & 0xFFu) / 255.0);
    return 4;
}

// The world's ground-target reticle at the actor's position (Scene.cpp,
// DrawGroundReticle). A radius of 0 takes the world's default.
int __fastcall Script_SetGroundReticle(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (Game::Lua::IsNumber(L, 2) == 0 || !(FloatArg(L, 2) >= 0.0f)) {
        Game::Lua::Error(L, "Usage: %s:SetGroundReticle(radius [, valid])", actor->DisplayName());
        return 0;
    }
    actor->groundReticleRadius = FloatArg(L, 2);
    actor->groundReticleValid = Game::Lua::Type(L, 3) <= Game::Lua::TYPE_NIL || Game::Lua::ToBoolean(L, 3) != 0;
    actor->groundReticleShown = true;
    return 0;
}

int __fastcall Script_ClearGroundReticle(void *L) {
    if (Actor *actor = Self(L))
        actor->groundReticleShown = false;
    return 0;
}

int __fastcall Script_GetGroundReticle(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !actor->groundReticleShown)
        return 0;
    Game::Lua::PushNumber(L, GroundReticleRadius(*actor));
    Game::Lua::PushBool(L, actor->groundReticleValid);
    return 2;
}

void ClearName(Actor &actor) {
    actor.nameText.clear();
    actor.DropNameString();
    InstallName(actor);
}

// A new text is made at the next draw; a new colour alone goes to the string
// made, as UpdatePlate's colour refetch does (GxuStringSetColor).
int __fastcall Script_SetNameText(void *L) {
    constexpr char kUsage[] = "SetNameText(text [, colorR, colorG, colorB])";
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    // The colour is optional: a nil third argument (UnitName's realm) is none.
    const bool colored = Game::Lua::IsNumber(L, 3) != 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:%s", actor->DisplayName(), kUsage);
        return 0;
    }
    if (colored && !NumberArgs(L, 3, 3, actor->DisplayName(), kUsage))
        return 0;
    const char *text = Game::Lua::ToString(L, 2);
    if (*text == '\0') {
        ClearName(*actor);
        return 0;
    }
    uint32_t color = 0xFFFFFFFFu;
    if (colored)
        color = 0xFF000000u | ColorByte(static_cast<float>(Game::Lua::ToNumber(L, 3))) << 16 |
                ColorByte(static_cast<float>(Game::Lua::ToNumber(L, 4))) << 8 |
                ColorByte(static_cast<float>(Game::Lua::ToNumber(L, 5)));
    const bool sameText = actor->nameText == text;
    actor->nameColor = color;
    if (sameText && actor->nameString != nullptr && !actor->nameDirty) {
        reinterpret_cast<void(__fastcall *)(void *, const uint32_t *)>(Offsets::FUN_GXU_STRING_SET_COLOR)(
            actor->nameString, &actor->nameColor);
        return 0;
    }
    actor->nameText = text;
    actor->nameDirty = true;
    InstallName(*actor);
    return 0;
}

int __fastcall Script_ClearNameText(void *L) {
    if (Actor *actor = Self(L))
        ClearName(*actor);
    return 0;
}

int __fastcall Script_GetNameText(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || actor->nameText.empty())
        return 0;
    Game::Lua::PushString(L, actor->nameText.c_str());
    for (int shift : {16, 8, 0})
        Game::Lua::PushNumber(L, ((actor->nameColor >> shift) & 0xFFu) / 255.0);
    return 4;
}

int __fastcall Script_GetNamePosition(void *L) {
    Actor *actor = Self(L);
    float at[3];
    if (actor == nullptr || !actor->NamePosition(at))
        return 0;
    for (float v : at)
        Game::Lua::PushNumber(L, v);
    return 3;
}

int __fastcall Script_DetachFromActor(void *L) {
    Actor *child = Self(L);
    if (child == nullptr)
        return 0;
    const bool attached = child->model != nullptr && M2::Parent(child->model) != nullptr;
    if (attached)
        M2::Detach(child->model);
    Game::Lua::PushBool(L, attached);
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

// ---- the mover (a ClassicAPI extension) ---------------------------------------------

// The collision box the client gives a unit of the actor's display
// (CGUnit_C::UpdateCollisionSize 0x0060B270), the actor's scale standing in
// for the unit's; a model file keeps the constructor's box.
void SizeMover(Actor &actor) {
    constexpr float kNoBox = 9.5367431640625e-7f; // 2^-20, its NOCOLLISIONBOX bound
    if (actor.displayID == 0)
        return;
    const uint8_t *display = DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_RECORDS,
                                         Offsets::VAR_CREATUREDISPLAYINFO_COUNT, actor.displayID);
    if (display == nullptr)
        return;
    const uint8_t *data =
        DBC::Record(Offsets::VAR_CREATUREMODELDATA_RECORDS, Offsets::VAR_CREATUREMODELDATA_COUNT,
                    Game::Read<uint32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_MODEL_ID));
    if (data == nullptr)
        return;
    const float width = Game::Read<float>(data, Offsets::OFF_CREATUREMODELDATA_COLLISION_WIDTH);
    const float height = Game::Read<float>(data, Offsets::OFF_CREATUREMODELDATA_COLLISION_HEIGHT);
    const float displayScale = Game::Read<float>(display, Offsets::OFF_CREATUREDISPLAYINFO_SCALE);
    const float scale = actor.scale < displayScale ? displayScale : actor.scale;
    if (std::fabs(width) < kNoBox || std::fabs(height) < kNoBox || std::fabs(scale) < kNoBox)
        return;
    Mover::SetCollisionSize(*actor.mover, width, height, scale, displayScale);
}

// Puts the body down where the actor stands, as the client puts a unit down
// after a near teleport: the position reset (0x007C69A0), then the fall
// gate (0x007C61C0), so it falls to the floor below. As every move event
// that is not the player's own, it ends a MoveTo first.
void PutDown(Actor &actor) {
    Advance(actor);
    Mover::ClearSpline(*actor.mover);
    Mover::Reset(*actor.mover, actor.position, actor.yaw);
    Mover::FallGate(*actor.mover);
    Locomotion::OnMove(actor, Locomotion::Move::TeleportAck);
}

int __fastcall Script_SetMovementEnabled(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (Game::Lua::ToBoolean(L, 2) == 0) {
        if (actor->mover != nullptr && actor->model != nullptr && actor->loaded)
            ClearTwist(actor->model);
        actor->mover.reset();
        return 0;
    }
    if (actor->mover == nullptr) {
        actor->mover = std::make_unique<Mover::Body>();
        Mover::Init(*actor->mover, actor->position, actor->yaw, ClockMs(actor->SceneFrame()));
        actor->mover->facets.query = &World::QueryFacets;
        actor->mover->facets.liquidQuery = &World::QueryLiquidFacets;
        actor->mover->stepped = &Locomotion::OnStep;
        actor->mover->steppedContext = actor;
    }
    actor->mover->facets.context = WorldOf(actor->SceneFrame());
    SizeMover(*actor);
    PutDown(*actor);
    ResetFacings(*actor);
    return 0;
}

int __fastcall Script_IsMovementEnabled(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->mover != nullptr);
    return 1;
}

// What a command did: whether the mover took it, and the movement message
// the client then sends and animates (UpdatePlayerMovement 0x00615C30), if
// any.
struct Command {
    bool took;
    std::optional<Locomotion::Move> move;
};

// A movement command at the body's tick, as the client applies a command
// at its event time. The script stands in for the server, so a command is a
// move event that is not the player's own: it ends a MoveTo first
// (ClearSpline in UpdatePlayerMovement 0x00615C30). Nothing without the
// mover.
template <Command (*Apply)(Mover::Body &)>
int __fastcall MoverCommand(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || actor->mover == nullptr)
        return 0;
    Advance(*actor);
    Mover::ClearSpline(*actor->mover);
    const Command done = Apply(*actor->mover);
    if (done.move)
        Locomotion::OnMove(*actor, *done.move);
    Game::Lua::PushBool(L, done.took);
    return 1;
}

using Locomotion::Move;

// A command whose message goes only when the mover took it.
template <bool (*Run)(Mover::Body &), Move M>
Command Sent(Mover::Body &b) {
    const bool took = Run(b);
    return {took, took ? std::optional<Move>(M) : std::nullopt};
}

bool StartForward(Mover::Body &b) { return Mover::StartMove(b, true); }
bool StartBackward(Mover::Body &b) { return Mover::StartMove(b, false); }
bool StartStrafeLeft(Mover::Body &b) { return Mover::StartStrafe(b, true); }
bool StartStrafeRight(Mover::Body &b) { return Mover::StartStrafe(b, false); }
// The turns' messages go whatever the mover did (cases 8 to 10).
Command StartTurnLeft(Mover::Body &b) {
    Mover::StartTurn(b, true);
    return {true, Move::StartTurnLeft};
}
Command StartTurnRight(Mover::Body &b) {
    Mover::StartTurn(b, false);
    return {true, Move::StartTurnRight};
}
Command StopTurn(Mover::Body &b) { return {Mover::StopTurn(b), Move::StopTurn}; }
// The pitch commands act only while swimming, and animate nothing.
Command StartPitchUp(Mover::Body &b) {
    const bool swimming = (b.flags & Mover::FLAG_SWIMMING) != 0;
    Mover::StartPitch(b, true);
    return {swimming, std::nullopt};
}
Command StartPitchDown(Mover::Body &b) {
    const bool swimming = (b.flags & Mover::FLAG_SWIMMING) != 0;
    Mover::StartPitch(b, false);
    return {swimming, std::nullopt};
}
Command StopPitch(Mover::Body &b) { return {Mover::StopPitch(b), std::nullopt}; }

// The server's move (SMSG_MONSTER_MOVE, CGUnit_C::OnMonsterMove 0x006018F0):
// the mover's path, or its spline done at once; then the unit's side
// (0x006020ED, 0x006020FB): the ranged pose ends (EndRangedPose 0x0060F530)
// and the animation is picked again (UpdateBaseAnimation 0x005FD9E0). Left
// out: the stand state and the precast clear (UnitClearPrecastSpell
// 0x0060D040), which an actor's casts have no part for.
int __fastcall Script_MoveTo(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 3, actor->DisplayName(), "MoveTo(x, y, z [, facing])"))
        return 0;
    if (actor->mover == nullptr)
        return 0;
    const float to[3] = {FloatArg(L, 2), FloatArg(L, 3), FloatArg(L, 4)};
    const bool faces = Game::Lua::IsNumber(L, 5) != 0;
    const float facing = faces ? FloatArg(L, 5) : 0.0f;
    Advance(*actor);
    if (!Mover::MoveTo(*actor->mover, to, faces ? &facing : nullptr))
        actor->movesFinished += 1;
    Casts::EndRangedPose(*actor);
    Locomotion::Repick(*actor);
    return 0;
}

// A server's knockback (Unit::KnockBackFrom): the direction from the source
// to the unit, or its facing turned half a circle when it stands on the
// source; the vertical speed negated into the client's down-positive fall
// speed; the unit's spells interrupted (InterruptNonMeleeSpells). The client
// takes it as a move event that is not the player's own, so a MoveTo ends
// first (ClearSpline), then launches (0x006179C0) and animates the
// acknowledgement (0xF0: Fall). False without the mover.
int __fastcall Script_KnockBack(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 4, actor->DisplayName(),
                                        "KnockBack(fromX, fromY, horizontalSpeed, verticalSpeed)"))
        return 0;
    if (actor->mover == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Advance(*actor);
    const float dx = actor->mover->position[0] - FloatArg(L, 2);
    const float dy = actor->mover->position[1] - FloatArg(L, 3);
    const float angle =
        dx == 0.0f && dy == 0.0f ? actor->mover->facing + Game::Read<float>(Offsets::VAR_PI) : std::atan2(dy, dx);
    Casts::Stop(*actor);
    Mover::ClearSpline(*actor->mover);
    Mover::KnockBack(*actor->mover, std::cos(angle), std::sin(angle), FloatArg(L, 4), -FloatArg(L, 5));
    Locomotion::OnMove(*actor, Move::KnockBack);
    Game::Lua::PushBool(L, true);
    return 1;
}

// MSG_MOVE_SET_WALK_MODE and MSG_MOVE_SET_RUN_MODE (cases 0xF and 0xE of
// UpdatePlayerMovement 0x00615C30): the run mode (SetRunMode 0x007C71C0),
// which a moving body animates.
int __fastcall Script_SetWalking(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || actor->mover == nullptr)
        return 0;
    const bool walking = Game::Lua::ToBoolean(L, 2) != 0;
    Advance(*actor);
    Mover::ClearSpline(*actor->mover);
    Mover::SetRunMode(*actor->mover, !walking);
    Locomotion::OnMove(*actor, walking ? Move::WalkMode : Move::RunMode);
    return 0;
}

// The speed names of SetMovementSpeed and GetMovementSpeed.
bool SpeedArg(void *L, int index, Mover::Speed &which) {
    static const struct {
        const char *name;
        Mover::Speed speed;
    } kSpeeds[] = {
        {"walk", Mover::Speed::Walk},   {"run", Mover::Speed::Run},
        {"runBack", Mover::Speed::RunBack}, {"swim", Mover::Speed::Swim},
        {"swimBack", Mover::Speed::SwimBack}, {"turn", Mover::Speed::Turn},
    };
    const char *name = Game::Lua::IsString(L, index) ? Game::Lua::ToString(L, index) : nullptr;
    if (name == nullptr)
        return false;
    for (const auto &entry : kSpeeds) {
        if (std::strcmp(name, entry.name) == 0) {
            which = entry.speed;
            return true;
        }
    }
    return false;
}

constexpr char kUsageSetMovementSpeed[] =
    "Usage: %s:SetMovementSpeed(\"walk\", \"run\", \"runBack\", \"swim\", \"swimBack\" or \"turn\", speed)";

// A server's speed change for a unit it moves (the spline forms of
// SMSG_SPLINE_SET_*_SPEED, CGUnit_C 0x00601360): the mover's setter at once,
// then AnimateMoveOpcode (0x0060E480, nothing for these opcodes) and
// UpdateBaseAnimation(-1) (0x005FD9E0), so a moving unit takes the new
// speed's animation and rate. The setter does not touch a spline: a MoveTo
// keeps the speed it started with.
int __fastcall Script_SetMovementSpeed(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Mover::Speed which{};
    if (!SpeedArg(L, 2, which) || Game::Lua::IsNumber(L, 3) == 0 || !(FloatArg(L, 3) > 0.0f)) {
        Game::Lua::Error(L, kUsageSetMovementSpeed, actor->DisplayName());
        return 0;
    }
    if (actor->mover == nullptr)
        return 0;
    Advance(*actor);
    if (Mover::SetSpeed(*actor->mover, which, FloatArg(L, 3)))
        Locomotion::Repick(*actor);
    return 0;
}

int __fastcall Script_GetMovementSpeed(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Mover::Speed which{};
    if (!SpeedArg(L, 2, which)) {
        Game::Lua::Error(L, "Usage: %s:GetMovementSpeed(\"walk\", \"run\", \"runBack\", \"swim\", \"swimBack\" or \"turn\")",
                         actor->DisplayName());
        return 0;
    }
    if (actor->mover == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Mover::GetSpeed(*actor->mover, which));
    return 1;
}

int __fastcall Script_IsWalking(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->mover != nullptr && (actor->mover->flags & Mover::FLAG_WALK) != 0);
    return 1;
}

int __fastcall Script_IsSwimming(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->mover != nullptr && (actor->mover->flags & Mover::FLAG_SWIMMING) != 0);
    return 1;
}

int __fastcall Script_IsFalling(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || actor->mover == nullptr || (actor->mover->flags & Mover::FLAG_FALLING) == 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L, true);
    Game::Lua::PushNumber(L, -Mover::FallSpeed(*actor->mover));
    return 2;
}

// The pivot height the world's follow camera eases to for its target, from
// the actor's model as the camera reads a unit's. CCamera::BuildPivotHeights
// (0x0050CA90) makes three presets, which the camera's target setter
// (0x0050D0F0) rebuilds every frame once the model has loaded:
//
//   - the base, the same in each: the height of the mouth (attachment 17,
//     its unposed offset) plus 7/72 yard, or without one 0.9 of the
//     collision box's height, times the scale;
//   - near: lowered by how much lower the stand state's animation reaches
//     than Stand (0), each sequence's bounds top times the scale;
//   - swimming: lowered the same way by Swim (42), when the model has both;
//
// each kept from 5/6 to 15 yards. ArmPivotHeight (0x0050F880) picks the
// swimming one for a swimmer, else near while the target distance is under
// 1.8315 yards, else far.
//
// The scale is the actor's own, the size its model is drawn at, where a
// unit's is its descriptor scale times its model scales (0x0050CB24). An
// actor has no stand state: the animation it plays stands in for it when it
// is one the stand states give (the jump table 0x0050D0CC), and
// cameraHeightIgnoreStandState ignores it as it does a unit's. Left out: a
// mount, whose model a mounted unit's camera reads in place of the body's
// (0x0050CE10 on), as an actor rides by hanging on another actor.
namespace {
constexpr uint32_t kMouthAttachment = 17;

uint32_t StanceAnimation(uint32_t animation) {
    switch (animation) {
    case Offsets::ANIM_SIT_GROUND: // stand state 1
    case Offsets::ANIM_SLEEP:      // 3
    case Offsets::ANIM_SIT_CHAIR_LOW:
    case Offsets::ANIM_SIT_CHAIR_MED:
    case Offsets::ANIM_SIT_CHAIR_HIGH:
    case Offsets::ANIM_DEAD:       // 7
    case Offsets::ANIM_KNEEL_LOOP: // 8
        return animation;
    default:
        return Offsets::ANIM_STAND;
    }
}

float SequenceTop(void *model, uint32_t animation, float scale) {
    return M2::GetSequenceInfo(model, animation, 0).bounds[5] * scale;
}

float ClampPivot(float height) {
    const float low = Game::Live<float>{Offsets::VAR_CAMERA_PIVOT_MIN};
    const float high = Game::Live<float>{Offsets::VAR_CAMERA_PIVOT_MAX};
    return height < low ? low : (height < high ? height : high);
}
} // namespace

int __fastcall Script_GetCameraPivotHeight(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), "GetCameraPivotHeight(distance)"))
        return 0;
    void *model = actor->model;
    if (model == nullptr || !M2::IsLoaded(model, 0, 0))
        return 0;
    const float distance = FloatArg(L, 2);
    const float scale = actor->scale;

    uint32_t stance = StanceAnimation(actor->animation);
    if (CVar::Factory::GetInt(Game::Read<void *>(Offsets::VAR_CVAR_CAMERA_HEIGHT_IGNORE_STAND_STATE)) != 0)
        stance = Offsets::ANIM_STAND;
    float base;
    if (M2::HasAttachment(model, kMouthAttachment)) {
        float mouth[3];
        M2::GetAttachmentOffset(model, kMouthAttachment, mouth);
        base = (mouth[2] + Game::Live<float>{Offsets::VAR_CAMERA_PIVOT_LIFT}) * scale;
    } else {
        float box[6];
        M2::GetCollisionBox(model, box);
        base = (box[5] - box[2]) * scale * Game::Live<float>{Offsets::VAR_NINE_TENTHS};
    }
    float nearHeight = base, farHeight = base, swimHeight = base;
    if (!M2::HasAnimation(model, stance))
        stance = Offsets::ANIM_STAND;
    if (M2::HasAnimation(model, Offsets::ANIM_STAND)) {
        const float standTop = SequenceTop(model, Offsets::ANIM_STAND, scale);
        if (M2::HasAnimation(model, Offsets::ANIM_SWIM))
            swimHeight -= standTop - SequenceTop(model, Offsets::ANIM_SWIM, scale);
        nearHeight -= standTop - SequenceTop(model, stance, scale);
    }

    float height;
    if (actor->mover != nullptr && (actor->mover->flags & Mover::FLAG_SWIMMING) != 0)
        height = swimHeight;
    else if (distance < Game::Live<float>{Offsets::VAR_CAMERA_NEAR_DISTANCE})
        height = nearHeight;
    else
        height = farHeight;
    Game::Lua::PushNumber(L, ClampPivot(height));
    return 1;
}

int __fastcall Script_SetPosition(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 3, actor->DisplayName(), "SetPosition(x, y, z)"))
        return 0;
    for (int i = 0; i < 3; ++i)
        actor->position[i] = FloatArg(L, 2 + i);
    if (actor->mover != nullptr)
        PutDown(*actor);
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
int __fastcall Script_SetYaw(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), "SetYaw(yaw)"))
        return 0;
    actor->yaw = FloatArg(L, 2);
    if (actor->mover != nullptr) {
        Advance(*actor);
        Mover::ClearSpline(*actor->mover); // MSG_MOVE_SET_FACING (case 0x10)
        Mover::SetFacing(*actor->mover, actor->yaw);
    }
    return 0;
}

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
    // Showing or hiding ends a fade out; the actor's alpha returns.
    if (actor.fadingOut) {
        actor.fadingOut = false;
        actor.ApplyAlpha();
    }
}

// A removed object's fade (deactivate 0x006145E0 -> FadeOutModel
// 0x00672DF0): a ready model (0x00710450 with 0, 0) shown at an alpha of
// 0.01 or more, or NaN, fades over 2 seconds; any other goes at once. The
// world's clock is the OS tick; here it is the scene's, so a paused scene
// holds the fade.
int __fastcall Script_FadeOut(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !actor->shown || actor->fadingOut)
        return 0;
    const float shownAlpha =
        actor->alpha * (Game::Read<uint8_t>(actor->SceneFrame(), Offsets::OFF_FRAME_ALPHA) / 255.0f);
    if (actor->model == nullptr || !M2::IsReady(actor->model, 0, 0) ||
        shownAlpha < Game::Read<float>(Offsets::VAR_ONE_HUNDREDTH)) {
        SetShown(*actor, false);
        return 0;
    }
    actor->fadingOut = true;
    actor->fadeStartMs = ClockMs(actor->SceneFrame());
    return 0;
}

int __fastcall Script_IsFadingOut(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->fadingOut);
    return 1;
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
    actor->kitAnimation = -1; // the script's animation replaces a spell kit's
    actor->kitAnimationLoops = false;
    actor->kitLoopAnimation = -1;
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

const Game::Lua::EnumIntegerEntry kSheatheCategory[] = {
    {"Default", Outfit::kSheatheDefault},
    {"Back", Outfit::kSheatheBack},
    {"Side", Outfit::kSheatheSide},
    {"Hide", Outfit::kSheatheHide},
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
    {"SetModelBySpellArea", &Script_SetModelBySpellArea},
    {"SetModelByUnit", &Script_SetModelByUnit},
    {"SetModelByUnitCreatureDisplayID", &Script_SetModelByUnitCreatureDisplayID},
    {"GetModelUnitGUID", &Script_GetModelUnitGUID},
    {"Dress", &Script_Dress},
    {"Undress", &Script_Undress},
    {"UndressSlot", &Script_UndressSlot},
    {"TryOn", &Script_TryOn},
    {"DressPlayerSlot", &Script_DressPlayerSlot},
    {"IsSlotAllowed", &Script_IsSlotAllowed},
    {"IsSlotVisible", &Script_IsSlotAllowed},
    {"GetItemTransmogInfo", &Script_GetItemTransmogInfo},
    {"GetItemTransmogInfoList", &Script_GetItemTransmogInfoList},
    {"SetItemTransmogInfo", &Script_SetItemTransmogInfo},
    {"SetSheathed", &Script_SetSheathed},
    {"GetSheathed", &Script_GetSheathed},
    {"SetSheathedCategory", &Script_SetSheathedCategory},
    {"UseUnitSheatheCategory", &Script_UseUnitSheatheCategory},
    {"SetAutoDress", &Script_SetAutoDress},
    {"GetAutoDress", &Script_GetAutoDress},
    {"IsGeoReady", &Script_IsGeoReady},
    {"SetPaused", &Script_SetPaused},
    {"GetPaused", &Script_GetPaused},
    {"AttachToMount", &Script_AttachToMount},
    {"DetachFromMount", &Script_DetachFromMount},
    {"CalculateMountScale", &Script_CalculateMountScale},
    {"GetAttachmentPosition", &Script_GetAttachmentPosition},
    {"AttachToActor", &Script_AttachToActor},
    {"DetachFromActor", &Script_DetachFromActor},
    {"PlaySpellVisual", &Script_PlaySpellVisual},
    {"StopSpellVisual", &Script_StopSpellVisual},
    {"ApplySpellVisualKit", &Script_ApplySpellVisualKit},
    {"PlaySound", &Script_PlaySound},
    {"SetPlayAnimationSounds", &Script_SetPlayAnimationSounds},
    {"GetPlayAnimationSounds", &Script_GetPlayAnimationSounds},
    {"SetShadowShown", &Script_SetShadowShown},
    {"SetHighlight", &Script_SetHighlight},
    {"IsHighlighted", &Script_IsHighlighted},
    {"SetSelectionCircle", &Script_SetSelectionCircle},
    {"ClearSelectionCircle", &Script_ClearSelectionCircle},
    {"GetSelectionCircle", &Script_GetSelectionCircle},
    {"SetGroundReticle", &Script_SetGroundReticle},
    {"ClearGroundReticle", &Script_ClearGroundReticle},
    {"GetGroundReticle", &Script_GetGroundReticle},
    {"SetNameText", &Script_SetNameText},
    {"ClearNameText", &Script_ClearNameText},
    {"GetNameText", &Script_GetNameText},
    {"GetNamePosition", &Script_GetNamePosition},
    {"IsShadowShown", &Script_IsShadowShown},
    {"SetMovementEnabled", &Script_SetMovementEnabled},
    {"IsMovementEnabled", &Script_IsMovementEnabled},
    {"StartMovingForward", &MoverCommand<&Sent<&StartForward, Move::StartForward>>},
    {"StartMovingBackward", &MoverCommand<&Sent<&StartBackward, Move::StartBackward>>},
    {"StopMoving", &MoverCommand<&Sent<&Mover::StopMove, Move::Stop>>},
    {"StartStrafingLeft", &MoverCommand<&Sent<&StartStrafeLeft, Move::StartStrafeLeft>>},
    {"StartStrafingRight", &MoverCommand<&Sent<&StartStrafeRight, Move::StartStrafeRight>>},
    {"StopStrafing", &MoverCommand<&Sent<&Mover::StopStrafe, Move::StopStrafe>>},
    {"StartTurningLeft", &MoverCommand<&StartTurnLeft>},
    {"StartTurningRight", &MoverCommand<&StartTurnRight>},
    {"StopTurning", &MoverCommand<&StopTurn>},
    {"Jump", &MoverCommand<&Sent<&Mover::Jump, Move::Jump>>},
    {"StartPitchingUp", &MoverCommand<&StartPitchUp>},
    {"StartPitchingDown", &MoverCommand<&StartPitchDown>},
    {"StopPitching", &MoverCommand<&StopPitch>},
    {"IsFalling", &Script_IsFalling},
    {"IsSwimming", &Script_IsSwimming},
    {"GetCameraPivotHeight", &Script_GetCameraPivotHeight},
    {"MoveTo", &Script_MoveTo},
    {"KnockBack", &Script_KnockBack},
    {"SetWalking", &Script_SetWalking},
    {"IsWalking", &Script_IsWalking},
    {"SetMovementSpeed", &Script_SetMovementSpeed},
    {"GetMovementSpeed", &Script_GetMovementSpeed},
    {"ClearModel", &Script_ClearModel},
    {"IsLoaded", &Script_IsLoaded},
    {"GetModelPath", &Script_GetModelPath},
    {"SetPosition", &Script_SetPosition},
    {"GetPosition", &Script_GetPosition},
    {"SetYaw", &Script_SetYaw},
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
    {"FadeOut", &Script_FadeOut},
    {"IsFadingOut", &Script_IsFadingOut},
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
    "Puts an item on the actor. Armor needs a character model. Arrows and bullets become the "
    "ammo that PlaySpellVisual shoots with a ranged spell (a ClassicAPI extension). The item goes "
    "on once its data and the model have loaded.",
    kTryOnArgs, kTryOnRet};
const Game::Doc::Field kDressSlotArgs[] = {
    Req("invSlot", "luaIndex", "An inventory slot, 1 (head) to 19 (tabard)."),
};
const Game::Doc::Function kDressPlayerSlot{
    "Puts back what the actor's unit wears in one inventory slot.", kDressSlotArgs, {}};
const Game::Doc::Field kSlotAllowedRet[] = {Req("allowed", "bool")};
const Game::Doc::Field kSlotVisibleRet[] = {Req("visible", "bool")};
const Game::Doc::Function kIsSlotAllowed{
    "Returns whether the actor can wear an item in an inventory slot.", kUndressSlotArgs,
    kSlotAllowedRet};
const Game::Doc::Function kIsSlotVisible{
    "Returns whether an item in an inventory slot shows on the actor. Same as IsSlotAllowed.",
    kUndressSlotArgs, kSlotVisibleRet};
const Game::Doc::Field kTransmogInfoRet[] = {
    Req("itemTransmogInfo", "ItemTransmogInfo",
        "What the slot wears: appearanceID is the item ID, illusionID the enchant whose glow a "
        "weapon shows. Nil when the actor shows no model."),
};
const Game::Doc::Function kGetItemTransmogInfo{
    "Returns what the actor wears in one inventory slot.", kUndressSlotArgs, kTransmogInfoRet};
const Game::Doc::Field kTransmogInfoListRet[] = {
    Req("infoList", "table", "An ItemTransmogInfo for each inventory slot 1 to 19."),
};
const Game::Doc::Function kGetItemTransmogInfoList{
    "Returns what the actor wears in each inventory slot.", {}, kTransmogInfoListRet};
const Game::Doc::Field kSetTransmogInfoArgs[] = {
    Req("transmogInfo", "ItemTransmogInfo",
        "appearanceID is an item ID, illusionID a SpellItemEnchantment ID."),
    Opt("inventorySlots", "number", nullptr, "16 or 17: the hand that holds a weapon."),
    Opt("ignoreChildItems", "bool", "false", "Has no effect."),
};
const Game::Doc::Function kSetItemTransmogInfo{
    "Puts an item on the actor, as TryOn does. Shows no error. An appearanceID of 0 changes "
    "nothing.",
    kSetTransmogInfoArgs, kTryOnRet};
const Game::Doc::Function kSetSheathed{
    "Puts the held items in the hands or the sheaths, or hides them.", kSheathedArgs, {}};
const Game::Doc::Function kGetSheathed{"Returns whether the held items are sheathed.", {},
                                       kSheathedRet};
const Game::Doc::Field kSheathedCategoryArgs[] = {
    Req("inventorySlots", "number", "16 or 17: the hand. Other slots change nothing."),
    Req("category", "TransmogOutfitSlotOptionSheatheCategory"),
};
const Game::Doc::Function kSetSheathedCategory{
    "Sets where the item in a hand hangs while sheathed: its own place, the back, the side, or "
    "hidden.",
    kSheathedCategoryArgs, {}};
const Game::Doc::Field kUseUnitSheatheCategoryArgs[] = {
    Req("useCategory", "bool",
        "True puts each held item at its own sheath place, in place of the SetSheathedCategory "
        "categories."),
};
const Game::Doc::Function kUseUnitSheatheCategory{
    "Sets whether the held items hang at the unit's sheath places.", kUseUnitSheatheCategoryArgs,
    {}};
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
const Game::Doc::Field kAttachmentIDArg[] = {
    Req("attachmentID", "number", "A point of the model: 1 the right hand, 2 the left hand."),
};
const Game::Doc::Field kAttachmentPositionRets[] = {
    Req("x", "number"), Req("y", "number"), Req("z", "number"),
};
const Game::Doc::Function kGetAttachmentPosition{
    "Returns where a point of the model stood in the last frame the scene drew. Nothing until "
    "the model loads, or when the model has no such point.",
    kAttachmentIDArg, kAttachmentPositionRets, nullptr, true};
const Game::Doc::Field kAttachToActorArgs[] = {
    Req("parent", "ModelSceneFrameActor", "Another actor of the scene."),
    Req("attachmentID", "number", "The point of the parent: 1 the right hand, 2 the left hand."),
};
const Game::Doc::Function kAttachToActor{
    "Hangs this actor on a point of the parent. Its position, yaw and scale then apply from that "
    "point. False when either actor shows no model, or when the parent hangs on this actor.",
    kAttachToActorArgs, kMountSuccessRet, nullptr, true};
const Game::Doc::Field kPlaySpellVisualArgs[] = {
    Req("spellID", "number"),
    Opt("target", "ModelSceneFrameActor", nullptr,
        "Another actor of the scene. Without one, the spell is cast on this actor."),
    Opt("castTimeMs", "number", nullptr,
        "How long the spell is prepared. Without it, the cast time of the spell."),
};
const Game::Doc::Field kPlaySpellVisualRet[] = {
    Opt("castID", "number", nullptr,
        "Identifies this cast in OnSpellVisualImpact. Nil when the spell has no visual or this "
        "actor shows no model."),
};
const Game::Doc::Function kPlaySpellVisual{
    "Plays the visual of a spell cast from this actor at the target: the preparation, the cast, "
    "the missile and the impact, with their animations and sounds, heard from the scene's camera. "
    "A ranged spell such as Auto Shot takes what it lacks from the actor's ranged weapon, and "
    "shoots the actor's ammo (from TryOn; without it, a plain arrow or bullet). When the spell "
    "reaches its target, this actor's OnSpellVisualImpact script runs with (spellID, target, "
    "castID). This occurs when the missile arrives, or at the cast for a spell with no missile. "
    "An interrupted spell does not reach its target.",
    kPlaySpellVisualArgs, kPlaySpellVisualRet, nullptr, true};
const Game::Doc::Function kStopSpellVisual{
    "Interrupts the spells this actor is still preparing from PlaySpellVisual: their preparation "
    "effects and animation stop. A spell already cast goes on, its missile included.",
    {}, {}, nullptr, true};
const Game::Doc::Field kApplySpellVisualKitArgs[] = {
    Req("spellVisualKitID", "number", "A SpellVisualKit ID."),
    Opt("oneShot", "bool", "false",
        "True plays the effects once; false keeps them until the model changes."),
};
const Game::Doc::Function kApplySpellVisualKit{
    "Adds a spell visual kit's effects to the model, next to the kits it already shows.",
    kApplySpellVisualKitArgs, {}, nullptr, true};
const Game::Doc::Field kPlaySoundArgs[] = {Req("soundKitID", "number")};
const Game::Doc::Field kPlaySoundRet[] = {
    Req("willPlay", "bool", "False when the sound does not play, also when the actor is past its range."),
    Opt("soundHandle", "number", nullptr, "For StopSound."),
};
const Game::Doc::Function kPlaySound{
    "Plays a sound once at the actor, heard from the scene's camera as it would be heard at that "
    "distance in the world.",
    kPlaySoundArgs, kPlaySoundRet, nullptr, true};
const Game::Doc::Field kAnimationSoundsArg[] = {Req("enabled", "bool")};
const Game::Doc::Function kSetPlayAnimationSounds{
    "Sets whether the model's animations play their sounds, as a unit's do in the world: fidgets, "
    "the stand vocal, wing flaps, and on a scene with a ground terrain, footsteps and the death thud. They "
    "are heard from the scene's camera. Off by default.",
    kAnimationSoundsArg, {}, nullptr, true};
const Game::Doc::Function kGetPlayAnimationSounds{
    "Returns whether the model's animations play their sounds.", {}, kAnimationSoundsArg, nullptr,
    true};
const Game::Doc::Field kShadowShownArg[] = {Req("shown", "bool")};
const Game::Doc::Function kSetShadowShown{
    "Sets whether a blob shadow shows under the model, as under a unit in the world, while the "
    "shadowLOD cvar is 1. Off by default.",
    kShadowShownArg, {}, nullptr, true};
const Game::Doc::Function kIsShadowShown{"Returns whether the actor shows a blob shadow.", {},
                                         kShadowShownArg, nullptr, true};
const Game::Doc::Field kSetHighlightArgs[] = {
    Req("kind", "cstring", "\"target\" or \"mouseover\"."),
    Req("highlighted", "bool"),
};
const Game::Doc::Function kSetHighlight{
    "Sets or clears one kind of highlight. While either kind is set, the model glows in the ambient "
    "colour of the scene light, as a unit in the world glows under the cursor or as the target.",
    kSetHighlightArgs, {}, nullptr, true};
const Game::Doc::Field kIsHighlightedArgs[] = {
    Opt("kind", "cstring", nullptr, "\"target\" or \"mouseover\". Without it, either kind counts."),
};
const Game::Doc::Field kIsHighlightedRet[] = {Req("highlighted", "bool")};
const Game::Doc::Function kIsHighlighted{"Returns whether the actor has the highlight.", kIsHighlightedArgs,
                                         kIsHighlightedRet, nullptr, true};
const Game::Doc::Field kSelectionCircleColor[] = {
    Req("colorR", "number"), Req("colorG", "number"), Req("colorB", "number"),
    Opt("a", "number", "1", "The brightness: the circle adds its colour by this alpha."),
};
const Game::Doc::Function kSetSelectionCircle{
    "Shows the world's selection circle under the model in a colour, sized to the model as a unit's. "
    "It shows while the ObjectSelectionCircle cvar is on.",
    kSelectionCircleColor, {}, nullptr, true};
const Game::Doc::Function kClearSelectionCircle{"Hides the selection circle.", {}, {}, nullptr, true};
const Game::Doc::Field kReticleArgs[] = {
    Req("radius", "number", "The radius in yards. 0 takes the size of the world's reticle."),
    Opt("valid", "bool", "true", "False shows the out-of-range reticle."),
};
const Game::Doc::Field kReticleRet[] = {Req("radius", "number"), Req("valid", "bool")};
const Game::Doc::Function kSetGroundReticle{
    "Shows the world's ground-target reticle on the ground at the actor's position, as the world shows "
    "where a spell such as Blizzard will land.",
    kReticleArgs, {}, nullptr, true};
const Game::Doc::Function kClearGroundReticle{"Hides the ground-target reticle.", {}, {}, nullptr, true};
const Game::Doc::Function kGetGroundReticle{
    "Returns the reticle's radius and whether it shows in range, or nothing when it is hidden.", {},
    kReticleRet, nullptr, true};
const Game::Doc::Function kGetSelectionCircle{
    "Returns the colour of the selection circle. Returns nothing while the actor has none.", {},
    kSelectionCircleColor, nullptr, true};
const Game::Doc::Field kNameTextArgs[] = {
    Req("text", "string", "Lines split by new lines. An empty text removes the name."),
    Opt("colorR", "number", "1"), Opt("colorG", "number", "1"), Opt("colorB", "number", "1"),
};
const Game::Doc::Function kSetNameText{
    "Shows a name over the actor's head, drawn as the world draws a unit's name: the same font, at the "
    "head of the model, larger for a tall model.",
    kNameTextArgs, {}, nullptr, true};
const Game::Doc::Function kClearNameText{"Removes the name.", {}, {}, nullptr, true};
const Game::Doc::Field kNameTextRet[] = {
    Req("text", "string"), Req("colorR", "number"), Req("colorG", "number"), Req("colorB", "number"),
};
const Game::Doc::Function kGetNameText{"Returns the name and its colour. Returns nothing while the actor has none.",
                                       {}, kNameTextRet, nullptr, true};
const Game::Doc::Field kNamePositionRet[] = {Req("x", "number"), Req("y", "number"), Req("z", "number")};
const Game::Doc::Function kGetNamePosition{
    "Returns the point over the head where a unit's name sits, in scene coordinates: the model's name "
    "attachment, or a mount's for a seated rider. Returns nothing until the model has loaded.",
    {}, kNamePositionRet, nullptr, true};
const Game::Doc::Field kMovementEnabledArg[] = {Req("enabled", "bool")};
const Game::Doc::Function kSetMovementEnabled{
    "Sets whether the actor moves as a character moves in the world: it walks and runs over the scene's "
    "WMO, steps up and down ledges, slides along walls, falls, jumps, and swims in the WMO's liquids, at "
    "a character's speeds. The body is the size the world gives the actor's creature display, at the "
    "actor's scale. When you turn it on, and when you set the position while it is on, the actor falls "
    "to the floor below it. While it is on, the actor's position, yaw and pitch follow the movement: "
    "the actor tilts up or down only while it swims forward or backward. The model's body turns toward "
    "the side it strafes to; standing, its shoulders and head turn first and the body follows past a "
    "quarter turn. While it is on, the actor also plays a character's movement animations. It walks, "
    "runs, backs up, shuffles when it turns in place, swims, jumps, falls and lands as a character "
    "does, and its move animations play at the speed it moves. An animation that you set plays until "
    "it ends or the movement changes. Then the actor plays its movement animation again. Without a "
    "loaded WMO the actor does not move. Off by default.",
    kMovementEnabledArg, {}, nullptr, true};
const Game::Doc::Function kIsMovementEnabled{"Returns whether the actor moves as a character.", {},
                                             kMovementEnabledArg, nullptr, true};
const Game::Doc::Field kCommandRet[] = {
    Req("applied", "bool", "False when the command was refused, or kept until the actor lands."),
};
const Game::Doc::Function kStartMovingForward{
    "Starts the actor moving forward: it runs, or walks while SetWalking is on. In the air the actor "
    "keeps the way it jumped or fell, and the command takes effect when it lands.",
    {}, kCommandRet, nullptr, true};
const Game::Doc::Function kStartMovingBackward{
    "Starts the actor walking backward. In the air it takes effect when the actor lands.", {}, kCommandRet,
    nullptr, true};
const Game::Doc::Function kStopMoving{
    "Stops the actor moving forward or backward. In the air it takes effect when the actor lands.", {},
    kCommandRet, nullptr, true};
const Game::Doc::Function kStartStrafingLeft{
    "Starts the actor moving to its left. In the air it takes effect when the actor lands.", {},
    kCommandRet, nullptr, true};
const Game::Doc::Function kStartStrafingRight{
    "Starts the actor moving to its right. In the air it takes effect when the actor lands.", {},
    kCommandRet, nullptr, true};
const Game::Doc::Function kStopStrafing{
    "Stops the actor moving to the side. In the air it takes effect when the actor lands.", {},
    kCommandRet, nullptr, true};
const Game::Doc::Function kStartTurningLeft{"Starts the actor turning to its left.", {}, kCommandRet,
                                            nullptr, true};
const Game::Doc::Function kStartTurningRight{"Starts the actor turning to its right.", {}, kCommandRet,
                                             nullptr, true};
const Game::Doc::Function kStopTurning{"Stops the actor turning.", {}, kCommandRet, nullptr, true};
const Game::Doc::Function kJump{"Makes the actor jump. False when it is already in the air.", {},
                                kCommandRet, nullptr, true};
const Game::Doc::Field kKnockBackArgs[] = {
    Req("fromX", "number", "The point the actor is thrown away from."),
    Req("fromY", "number"),
    Req("horizontalSpeed", "number", "Yards a second along the ground. Negative pulls toward the point."),
    Req("verticalSpeed", "number", "Yards a second up at the start."),
};
const Game::Doc::Field kKnockBackRet[] = {
    Req("launched", "bool", "False unless SetMovementEnabled is on."),
};
const Game::Doc::Function kKnockBack{
    "Throws the actor away from a point, as a knockback spell throws a character. A MoveTo ends, and the "
    "spells that the actor prepares stop. The actor flies until it lands, then stops.",
    kKnockBackArgs, kKnockBackRet, nullptr, true};
const Game::Doc::Field kIsFallingRets[] = {
    Req("falling", "bool", "True while the actor is in the air: jumping or falling."),
    Opt("verticalSpeed", "number", nullptr, "Yards a second, up positive."),
};
const Game::Doc::Function kIsFalling{
    "Returns whether the actor is in the air, and how fast it rises or falls.", {}, kIsFallingRets,
    nullptr, true};
const Game::Doc::Function kStartPitchingUp{
    "Starts the actor turning its swim upward. False when the actor does not swim.", {}, kCommandRet,
    nullptr, true};
const Game::Doc::Function kStartPitchingDown{
    "Starts the actor turning its swim downward. False when the actor does not swim.", {}, kCommandRet,
    nullptr, true};
const Game::Doc::Function kStopPitching{
    "Stops the actor turning its swim up or down. It keeps the angle it reached. False when it was not "
    "turning up or down.",
    {}, kCommandRet, nullptr, true};
const Game::Doc::Field kIsSwimmingRet[] = {Req("swimming", "bool")};
const Game::Doc::Function kIsSwimming{
    "Returns whether the actor swims. An actor swims when the liquid over its feet is deeper than three "
    "quarters of its height. It stays level in the water unless it swims up or down, and it stops "
    "swimming when it leaves the water or rises near the surface.",
    {}, kIsSwimmingRet, nullptr, true};
const Game::Doc::Field kPivotDistanceArg[] = {
    Req("distance", "number", "The distance from the camera to the point it looks at, in yards."),
};
const Game::Doc::Field kPivotHeightRet[] = {Req("height", "number")};
const Game::Doc::Function kGetCameraPivotHeight{
    "Returns the height above the actor's feet that the camera of the world looks at for a character "
    "like this actor. The height comes from the model and the scale: the mouth, or the height of the "
    "model without a mouth. A camera nearer than about 1.83 yards looks lower when the actor sits, "
    "sleeps, kneels or lies dead. A swimming actor gives a lower height when its swim animation is "
    "lower than its stand animation. The height is from 5/6 to 15 yards. Returns nothing until the "
    "model has loaded.",
    kPivotDistanceArg, kPivotHeightRet, nullptr, true};
const Game::Doc::Field kMoveToArgs[] = {
    Req("x", "number"),
    Req("y", "number"),
    Req("z", "number"),
    Opt("facing", "number", nullptr, "The yaw that the actor turns to when it arrives."),
};
const Game::Doc::Function kMoveTo{
    "Moves the actor to the point along the ground, as the server moves a creature. The actor goes in a "
    "straight line at its own speed: it runs, or walks while SetWalking is on, or swims. It follows the "
    "floor, and it falls where the floor ends. A wall does not stop the move: when the actor is 3 yards "
    "or more from its place on the line, it goes to that place at once. When the actor arrives, it stops "
    "and turns to the facing, if you give one. Then its OnMoveFinished script runs. When the point is 1/6 "
    "yard away or less, the actor stops where it stands and OnMoveFinished runs at the next update. "
    "Another MoveTo, a "
    "movement command, SetWalking, SetPosition or SetYaw ends the move, and then OnMoveFinished does "
    "not run. Nothing happens unless SetMovementEnabled is on.",
    kMoveToArgs, {}, nullptr, true};
const Game::Doc::Field kWalkingArg[] = {Req("walking", "bool")};
const Game::Doc::Function kSetWalking{
    "Sets whether the actor walks instead of running when it moves forward, and in MoveTo. It ends a "
    "move that MoveTo started. Off by default.",
    kWalkingArg, {}, nullptr, true};
const Game::Doc::Function kIsWalking{"Returns whether the actor walks instead of running.", {},
                                     kWalkingArg, nullptr, true};
const Game::Doc::Field kSpeedNameArg[] = {
    Req("speedType", "string", "\"walk\", \"run\", \"runBack\", \"swim\", \"swimBack\" or \"turn\"."),
};
const Game::Doc::Field kSetMovementSpeedArgs[] = {
    Req("speedType", "string", "\"walk\", \"run\", \"runBack\", \"swim\", \"swimBack\" or \"turn\"."),
    Req("speed", "number", "Yards a second; radians a second for \"turn\". More than 0."),
};
const Game::Doc::Field kSpeedRet[] = {Req("speed", "number")};
const Game::Doc::Function kSetMovementSpeed{
    "Sets one of the speeds that the actor moves at. A move of MoveTo keeps the speed that it started "
    "with. The speeds go back to the defaults when SetMovementEnabled turns off. Nothing happens "
    "unless SetMovementEnabled is on.",
    kSetMovementSpeedArgs, {}, nullptr, true};
const Game::Doc::Function kGetMovementSpeed{
    "Returns one of the speeds that the actor moves at, or nothing unless SetMovementEnabled is on.",
    kSpeedNameArg, kSpeedRet, nullptr, true};
const Game::Doc::Function kDetachFromActor{
    "Takes this actor off the actor it hangs on. False when it hangs on none.", {},
    kMountSuccessRet, nullptr, true};

const Game::Doc::Function kSetModelByCreatureDisplayID{
    "Shows a creature display, dressed like the creature.", kDisplayArgs, kSuccessRet};
const Game::Doc::Function kSetModelByPath{"Shows a model file.", kPathArgs, kSuccessRet};
const Game::Doc::Field kSpellAreaArgs[] = {
    Req("spellID", "number", "A spell with a ground area, such as Blizzard or Consecration."),
    Req("radius", "number", "The radius of the area in yards. More than 0."),
};
const Game::Doc::Function kSetModelBySpellArea{
    "Shows the ground area of a spell, as the world shows the area that a spell such as Blizzard leaves, "
    "sized to the radius. False when the spell has no ground area.",
    kSpellAreaArgs, kSuccessRet, nullptr, true};
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
const Game::Doc::Function kFadeOut{
    "Fades the actor out over 2 seconds, as the world fades a unit that goes away, then hides it. Show and "
    "Hide end the fade.",
    {}, {}, nullptr, true};
const Game::Doc::Field kFadingOutRet[] = {Req("fadingOut", "bool")};
const Game::Doc::Function kIsFadingOut{"Returns whether the actor fades out.", {}, kFadingOutRet, nullptr, true};
const Game::Doc::Function kIsVisible{
    "Returns whether the actor is shown and its scene is visible.", {}, kVisibleRet};
const Game::Doc::Function kSetAnimation{
    "Plays an animation on the actor, again each time it ends. While movement is on "
    "(SetMovementEnabled), it plays once, and a change in movement can replace it first.",
    kAnimationArgs, {}};
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
    {"SetModelBySpellArea", &kSetModelBySpellArea},
    {"SetModelByUnit", &kSetModelByUnit},
    {"SetModelByUnitCreatureDisplayID", &kSetModelByUnitCreatureDisplayID},
    {"GetModelUnitGUID", &kGetModelUnitGUID},
    {"Dress", &kDress},
    {"Undress", &kUndress},
    {"UndressSlot", &kUndressSlot},
    {"TryOn", &kTryOn},
    {"DressPlayerSlot", &kDressPlayerSlot},
    {"IsSlotAllowed", &kIsSlotAllowed},
    {"IsSlotVisible", &kIsSlotVisible},
    {"GetItemTransmogInfo", &kGetItemTransmogInfo},
    {"GetItemTransmogInfoList", &kGetItemTransmogInfoList},
    {"SetItemTransmogInfo", &kSetItemTransmogInfo},
    {"SetSheathed", &kSetSheathed},
    {"GetSheathed", &kGetSheathed},
    {"SetSheathedCategory", &kSetSheathedCategory},
    {"UseUnitSheatheCategory", &kUseUnitSheatheCategory},
    {"SetAutoDress", &kSetAutoDress},
    {"GetAutoDress", &kGetAutoDress},
    {"IsGeoReady", &kIsGeoReady},
    {"SetPaused", &kSetPaused},
    {"GetPaused", &kGetPaused},
    {"AttachToMount", &kAttachToMount},
    {"DetachFromMount", &kDetachFromMount},
    {"CalculateMountScale", &kCalculateMountScale},
    {"GetAttachmentPosition", &kGetAttachmentPosition},
    {"AttachToActor", &kAttachToActor},
    {"DetachFromActor", &kDetachFromActor},
    {"PlaySpellVisual", &kPlaySpellVisual},
    {"StopSpellVisual", &kStopSpellVisual},
    {"ApplySpellVisualKit", &kApplySpellVisualKit},
    {"PlaySound", &kPlaySound},
    {"SetPlayAnimationSounds", &kSetPlayAnimationSounds},
    {"GetPlayAnimationSounds", &kGetPlayAnimationSounds},
    {"SetShadowShown", &kSetShadowShown},
    {"SetHighlight", &kSetHighlight},
    {"IsHighlighted", &kIsHighlighted},
    {"SetSelectionCircle", &kSetSelectionCircle},
    {"ClearSelectionCircle", &kClearSelectionCircle},
    {"GetSelectionCircle", &kGetSelectionCircle},
    {"SetGroundReticle", &kSetGroundReticle},
    {"ClearGroundReticle", &kClearGroundReticle},
    {"GetGroundReticle", &kGetGroundReticle},
    {"SetNameText", &kSetNameText},
    {"ClearNameText", &kClearNameText},
    {"GetNameText", &kGetNameText},
    {"GetNamePosition", &kGetNamePosition},
    {"IsShadowShown", &kIsShadowShown},
    {"SetMovementEnabled", &kSetMovementEnabled},
    {"IsMovementEnabled", &kIsMovementEnabled},
    {"StartMovingForward", &kStartMovingForward},
    {"StartMovingBackward", &kStartMovingBackward},
    {"StopMoving", &kStopMoving},
    {"StartStrafingLeft", &kStartStrafingLeft},
    {"StartStrafingRight", &kStartStrafingRight},
    {"StopStrafing", &kStopStrafing},
    {"StartTurningLeft", &kStartTurningLeft},
    {"StartTurningRight", &kStartTurningRight},
    {"StopTurning", &kStopTurning},
    {"Jump", &kJump},
    {"StartPitchingUp", &kStartPitchingUp},
    {"StartPitchingDown", &kStartPitchingDown},
    {"StopPitching", &kStopPitching},
    {"IsFalling", &kIsFalling},
    {"IsSwimming", &kIsSwimming},
    {"GetCameraPivotHeight", &kGetCameraPivotHeight},
    {"MoveTo", &kMoveTo},
    {"KnockBack", &kKnockBack},
    {"SetWalking", &kSetWalking},
    {"IsWalking", &kIsWalking},
    {"SetMovementSpeed", &kSetMovementSpeed},
    {"GetMovementSpeed", &kGetMovementSpeed},
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
    {"FadeOut", &kFadeOut},
    {"IsFadingOut", &kIsFadingOut},
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
    Game::Lua::RegisterIntegerEnum("Enum", "TransmogOutfitSlotOptionSheatheCategory",
                                   kSheatheCategory, "TransmogOutfitConstants");
    Api::Documentation::NameWidgetSystem(&Actor::s_class, "FrameAPIModelSceneFrameActor");
    Api::Documentation::RecordFrameMethods(&Actor::s_class, kMethods, kMethodDocs);
}

} // namespace Model::Scene
