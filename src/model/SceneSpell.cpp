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

// `actor:PlaySpellVisual(spellID, target [, castTimeMs])`, a ClassicAPI
// extension: a spell's visual played between two actors the way the world
// plays it between two units. The world's chain lives on units, missile
// objects and world objects, none of which an actor has, so this is that
// chain on actor models, step for step:
//
//   1. Precast (SPELL_START, 0x006EC220): the visual's +0x04 kit on the
//      caster, its effects and its animation looping, its sound once, for the
//      cast time (the engine's own FUN_GET_CAST_TIME unless one is given).
//   2. Cast (SPELL_GO, benilla spell/go): the precast's effects and tint
//      drop; the +0x08 kit plays once. A visual with a missile (+0x18, a
//      speed above 0, a target that is not the caster) makes the missile
//      model (+0x1C's SpellVisualEffectName file, else Spells\ErrorCube.mdx,
//      as the launcher 0x0060A3D0), held until its release. Without one the
//      impact plays at once (0x006E8BF0).
//   3. Release (CGUnit_C::ReleaseMissiles, 0x0060C940): at the caster
//      model's $CSL / $CSR / $CST event, from the event's point (the unit's
//      event callback, 0x005FFBD0). Without the event it is
//      ReleaseMissilesFromHand (0x0060C9B0): the first of those events the
//      model has, at its current point, else the caster's position. The
//      world calls that when the cast kit has no animation (0x006E8293),
//      when the cast animation ends (0x005FC920) and 2000 ms after the queue
//      (0x0060A814). A missile whose model has not loaded waits.
//   4. Flight (CMissile::Launch 0x0061CEB0, 0x0061E2A0): the speed is fitted
//      so the flight ends when the server would have it end, `t = d/speed -
//      queued`, `speed = min(d/t, 100)` (100 for t <= 0); the model plays
//      144 InFlight (0 Stand for t = 0) and loops, faces its travel, and
//      re-aims each frame at the target's attachment: the visual's +0x24
//      through s_spellVisualAttachments, else 15, else 19, else the target's
//      position raised by 0.75 of its height (0x0061CF13, 0x0061D230). Its
//      flight sound (+0x28) loops until arrival, then fades over 0.15 s (the
//      missile's free, 0x0061CD70). It arrives when a frame's step passes
//      the remaining distance (0x0061E3E8).
//   5. Arrival (0x0061DC50): the target plays its wound animation, then the
//      +0x0C impact kit once. A +0x30 impact model stays at the arrival
//      point until it holds no live particle (the impact list, 0x0061E930).
//
// What the world does and this does not: the leading of a moving target
// (actors move by script, not by a mover's velocity), misses, resists and
// reflections (no combat), and arrows stuck in the target. The sounds play
// where they would for units, heard from the scene camera (model/SceneSound.cpp):
// the kits' at the actor, the missile's loop moving with the missile.
//
// A paused caster (SetPaused) holds its casts and missiles where they are.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "gx/Matrix.h"
#include "model/DisplayInfo.h"
#include "model/M2.h"
#include "model/VisualKit.h"
#include "sound/Play.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace Model::Scene {

EntityLight::EntityLight(void *frame) : frame(frame), ambience(World::NewAmbience()) {}

EntityLight::~EntityLight() { World::FreeAmbience(ambience); }

} // namespace Model::Scene

namespace Model::Scene::Casts {

namespace {

// The model's lighting callback, `__fastcall(instance, record, user)`.
void __fastcall OnEntityLighting(void * /*instance*/, void *record, EntityLight *light) {
    World::ActorLight(WorldOf(light->frame), *light->ambience, light->frame, record);
}

void LightAsEntity(void *model, std::unique_ptr<EntityLight> &light, void *frame) {
    light = std::make_unique<EntityLight>(frame);
    M2::SetRenderCallback(model, reinterpret_cast<const void *>(&OnEntityLighting), light.get());
}

// The model drops its callback to the frame's before the light goes.
void ReleaseLit(void *model, std::unique_ptr<EntityLight> &light) {
    if (light != nullptr)
        M2::SetRenderCallback(model, reinterpret_cast<const void *>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB),
                              light->frame);
    light.reset();
    M2::SetVisible(model, false);
    M2::Release(model);
}

// The node's re-placement when it moved and its light's ease, each tick.
void SettleLight(void *model, EntityLight *light, float elapsed) {
    if (model == nullptr || light == nullptr || !M2::IsLoaded(model, 0, 0))
        return;
    if (World::Backdrop *world = WorldOf(light->frame))
        World::Settle(*world, *light->ambience, light->frame, model, elapsed);
}

} // namespace

namespace {

// The release events, '$CSL', '$CSR', '$CST'.
constexpr uint32_t kReleaseEvents[] = {0x4C534324, 0x52534324, 0x54534324};
constexpr int kAnimStand = 0;
constexpr int kAnimCombatWound = 9;
constexpr int kAnimInFlight = 144;
constexpr float kReleaseTimeoutSec = 2.0f; // 0x0060A814
constexpr float kMaxSpeed = 100.0f;        // 0x0080C854
constexpr float kSoundFadeSec = 0.15f;     // the missile's free, 0x0061CD70
const Game::Live<float> kAimHeight{Offsets::VAR_THREE_QUARTERS};
constexpr uint32_t kFallbackAttachments[] = {15, 19};
constexpr char kErrorCube[] = "Spells\\ErrorCube.mdx";

const uint8_t *Kit(const uint8_t *visual, uintptr_t column) {
    return Model::VisualKit::Record(Game::Read<uint32_t>(visual, column));
}

int KitAnimation(const uint8_t *kit) {
    return kit != nullptr ? Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_ANIMATION) : -1;
}

const char *EffectFile(uint32_t effectID) {
    const uint8_t *effect = DBC::Record(Offsets::VAR_SPELLVISUALEFFECTNAME_RECORDS,
                                        Offsets::VAR_SPELLVISUALEFFECTNAME_COUNT, effectID);
    const char *file =
        effect != nullptr ? Game::Read<const char *>(effect, Offsets::OFF_SPELLVISUALEFFECTNAME_FILE)
                          : nullptr;
    return file != nullptr && *file != '\0' ? file : nullptr;
}

// A model and every parent loaded: what the position reads need, since they
// wait for files and animate the parents first.
bool ChainLoaded(void *model) {
    if (model == nullptr)
        return false;
    for (void *m = model; m != nullptr; m = M2::Parent(m))
        if (!M2::IsLoaded(m, 0, 0))
            return false;
    return true;
}

// The world's kit player on a unit, for a model: the effects (looping into
// `effects`, or once and owned by the model), the animation and the sound.
void PlayKit(Actor &actor, const uint8_t *kit, bool loops, std::vector<void *> *effects) {
    if (kit == nullptr || actor.model == nullptr)
        return;
    Model::VisualKit::Apply(actor.SceneFrame(), actor.model, kit, !loops, effects);
    const int anim = KitAnimation(kit);
    if (anim >= 0)
        actor.PlayKitAnimation(anim, loops);
    Sounds::Play(actor.SceneFrame(), Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_SOUND), actor.position);
}

bool HasTintProcedure(const uint8_t *kit) {
    constexpr int32_t kTint = 1, kAlpha = 14;
    for (int i = 0; i < 4; ++i) {
        const auto kind = Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_KIND + i * 4);
        if (kind == kTint || kind == kAlpha)
            return true;
    }
    return false;
}

bool IsRanged(const uint8_t *spell) {
    return (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) & Offsets::SPELL_ATTR_RANGED) != 0;
}

// UnitGetSpellVisual (0x0060D450) for the actor: the spell's visual, and for
// a ranged spell the empty fields filled from its ranged weapon's visual.
// False when neither exists.
bool BuildVisual(const Actor &caster, const uint8_t *spell, uint8_t (&out)[Offsets::SPELL_VISUAL_SIZE]) {
    using GetVisual_t = const uint8_t *(__fastcall *)(const uint8_t *spell);
    const uint8_t *own = reinterpret_cast<GetVisual_t>(Offsets::FUN_GET_SPELL_VISUAL)(spell);
    const uint8_t *weapon = nullptr;
    if (IsRanged(spell) && caster.rangedDisplay != nullptr)
        weapon = DBC::Record(Offsets::VAR_SPELLVISUAL_RECORDS, Offsets::VAR_SPELLVISUAL_COUNT,
                             Game::Read<uint32_t>(caster.rangedDisplay, Offsets::OFF_ITEMDISPLAYINFO_SPELL_VISUAL));
    if (own == nullptr && weapon == nullptr)
        return false;
    if (own != nullptr)
        std::memcpy(out, own, sizeof out);
    else
        std::memset(out, 0, sizeof out);
    if (weapon == nullptr)
        return true;
    const auto field = [&out](uintptr_t at) -> uint32_t & { return *reinterpret_cast<uint32_t *>(out + at); };
    const auto fill = [&](uintptr_t at) {
        if (field(at) == 0)
            field(at) = Game::Read<uint32_t>(weapon, at);
    };
    fill(Offsets::OFF_SPELLVISUAL_PRECAST_KIT);
    fill(Offsets::OFF_SPELLVISUAL_CAST_KIT);
    fill(Offsets::OFF_SPELLVISUAL_IMPACT_KIT);
    if (field(Offsets::OFF_SPELLVISUAL_HAS_MISSILE) == 0 &&
        Game::Read<uint32_t>(weapon, Offsets::OFF_SPELLVISUAL_HAS_MISSILE) != 0) {
        field(Offsets::OFF_SPELLVISUAL_HAS_MISSILE) = 1;
        field(Offsets::OFF_SPELLVISUAL_MISSILE_MODEL) =
            Game::Read<uint32_t>(weapon, Offsets::OFF_SPELLVISUAL_MISSILE_MODEL);
    }
    fill(Offsets::OFF_SPELLVISUAL_MISSILE_PATH);
    fill(Offsets::OFF_SPELLVISUAL_MISSILE_ATTACHMENT);
    fill(Offsets::OFF_SPELLVISUAL_MISSILE_SOUND);
    fill(Offsets::OFF_SPELLVISUAL_PRECAST_SOUND);
    return true;
}

// The ammo a ranged spell's packets carry (the server's WriteAmmoToPacket):
// a thrown weapon is its own ammo; a bow, crossbow or gun shoots the ammo
// tried on, else what the server gives an NPC (Rough Arrow, Light Shot).
void ShotAmmo(const Actor &caster, const uint8_t *spell, uint32_t &display, uint32_t &type) {
    display = type = 0;
    if (!IsRanged(spell) || caster.rangedDisplay == nullptr)
        return;
    const Outfit::HeldLane &held = caster.ranged;
    if (held.inventoryType == Offsets::INVTYPE_THROWN) {
        display = Game::Read<uint32_t>(caster.rangedDisplay, 0);
        type = Offsets::INVTYPE_THROWN;
        return;
    }
    if (caster.ammoDisplay != 0)
        display = caster.ammoDisplay;
    else if (held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_BOW ||
             held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_CROSSBOW)
        display = Offsets::NPC_ARROW_DISPLAY;
    else if (held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_GUN)
        display = Offsets::NPC_BULLET_DISPLAY;
    if (display != 0)
        type = Offsets::INVTYPE_AMMO;
}

// ---- the ranged pose ------------------------------------------------------------

bool IsGunOrCrossbow(const Outfit::HeldLane &held) {
    return held.itemClass == Offsets::ITEM_CLASS_WEAPON && (held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_GUN ||
                                                            held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_CROSSBOW);
}

// Whether the ammo stays off the string: a gun or crossbow is the ranged item.
bool KeepsAmmoOff(const Actor &actor) { return IsGunOrCrossbow(actor.ranged); }

// The ranged weapon's model (a unit's +0xD24), or null (none hung).
void *RangedModel(const Actor &actor) { return actor.rangedModel; }

void Nock(Actor &actor) {
    M2::Attach(actor.heldAmmo, actor.model, Offsets::ATTACH_BOW_RELEASE);
    Model::DisplayInfo::LightChildren(actor.SceneFrame(), actor.model);
}

// CreateLooseItemModel: an item display's model for `body`, of `kind`
// (LOOSE_ITEM_*), that the caller owns one reference on, or null.
void *LooseItemModel(void *body, uint32_t display, uint32_t kind) {
    using LooseItem_t = void *(__fastcall *)(void *model, const void *record, uint32_t kind);
    return reinterpret_cast<LooseItem_t>(Offsets::FUN_CREATE_LOOSE_ITEM_MODEL)(
        body, DBC::Record(Offsets::VAR_ITEMDISPLAYINFO_RECORDS, Offsets::VAR_ITEMDISPLAYINFO_COUNT, display), kind);
}

// SetAmmoDisplay (0x0060BA30): the ammo in hand for `display`, made again
// when it changes; nocked at once while the bow is drawn.
void HoldAmmo(Actor &actor, uint32_t display) {
    if (actor.heldAmmoDisplay == display && actor.heldAmmo != nullptr)
        return;
    if (actor.heldAmmo != nullptr) {
        if (M2::Parent(actor.heldAmmo) != nullptr)
            M2::Detach(actor.heldAmmo);
        M2::Release(actor.heldAmmo);
        actor.heldAmmo = nullptr;
    }
    actor.heldAmmoDisplay = display;
    if (display == 0 || actor.model == nullptr)
        return;
    const uint32_t kind = actor.ranged.inventoryType == Offsets::INVTYPE_THROWN ? Offsets::LOOSE_ITEM_WEAPON
                                                                                : Offsets::LOOSE_ITEM_AMMO;
    actor.heldAmmo = LooseItemModel(actor.model, display, kind);
    if (actor.heldAmmo != nullptr && !KeepsAmmoOff(actor) && actor.bowDrawn && kind == Offsets::LOOSE_ITEM_AMMO)
        Nock(actor);
}

} // namespace

void EndRangedPose(Actor &actor) {
    actor.bowDrawn = false;
    if (actor.heldAmmo != nullptr && M2::Parent(actor.heldAmmo) != nullptr)
        M2::Detach(actor.heldAmmo);
    if (void *ranged = RangedModel(actor)) {
        if (M2::GetAnimState(ranged, -1).id != Offsets::ANIM_BOW_RELEASE)
            M2::PlayAnimation(ranged, -1, Offsets::ANIM_STAND, -1, 0, 1.0f, 1, 1);
    }
}

namespace {

// PlayBowPull (0x00624CC0): the weapon's BowPull, fitted to end with the
// body's animation. A unit whose +0xD5C is 4 asks key bone 4 first; an actor
// has no such state and asks the whole model.
void PlayBowPull(Actor &actor) {
    using HasAnim_t = bool(__thiscall *)(void *model, uint32_t anim);
    void *ranged = RangedModel(actor);
    if (ranged == nullptr || !M2::IsLoaded(ranged, 0, 0) ||
        !reinterpret_cast<HasAnim_t>(Offsets::FUN_M2MODEL_HAS_ANIMATION)(ranged, Offsets::ANIM_BOW_PULL) ||
        actor.model == nullptr || !M2::IsLoaded(actor.model, 0, 0))
        return;
    const M2::AnimState state = M2::GetAnimState(actor.model, -1);
    if (state.latch != 0)
        return;
    const auto played = static_cast<int>(static_cast<double>(state.position) / state.speed);
    const int left = static_cast<int32_t>(state.hi) - played - static_cast<int32_t>(state.lo);
    if (left <= 0)
        return;
    const M2::SequenceInfo info = M2::GetSequenceInfo(ranged, Offsets::ANIM_BOW_PULL, 0);
    const float speed = static_cast<float>(info.length) / static_cast<float>(left);
    M2::PlayAnimation(ranged, -1, Offsets::ANIM_BOW_PULL, 0, 0, speed, 1, 1);
}

// The unit's current spell is ranged: here a cast still preparing or waiting
// for its release.
bool CastsRanged(const Actor &actor) {
    for (const SpellCast &cast : actor.spellCasts) {
        if (cast.phase == SpellCast::Phase::Flight)
            continue;
        const uint8_t *spell =
            DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT, cast.spellID);
        if (spell != nullptr && IsRanged(spell))
            return true;
    }
    return false;
}

bool MissileQueued(const Actor &actor) {
    for (const SpellCast &cast : actor.spellCasts)
        if (cast.phase == SpellCast::Phase::Cast && cast.missile != nullptr)
            return true;
    return false;
}

bool IsBowCrossbowOrThrown(const Outfit::HeldLane &held) {
    return held.itemClass == Offsets::ITEM_CLASS_WEAPON &&
           (held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_BOW || held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_CROSSBOW ||
            held.subclass == Offsets::ITEM_SUBCLASS_WEAPON_THROWN);
}

void FreeStuck(StuckMissile &stuck) {
    if (M2::Parent(stuck.model) != nullptr)
        M2::Detach(stuck.model);
    M2::SetVisible(stuck.model, false);
    M2::Release(stuck.model);
    stuck.model = nullptr;
}

void EndPrecast(Actor &caster, SpellCast &cast) {
    for (void *effect : cast.precastEffects) {
        M2::Detach(effect);
        M2::Release(effect);
    }
    cast.precastEffects.clear();
    if (cast.precastTinted && caster.model != nullptr) {
        const float white[3] = {1.0f, 1.0f, 1.0f};
        M2::SetDiffuse(caster.model, white);
        caster.ApplyAlpha();
        cast.precastTinted = false;
    }
    // The loop goes; a once-only animation over it (a wound) plays on.
    caster.kitLoopAnimation = -1;
    if (caster.kitAnimationLoops) {
        caster.kitAnimation = -1;
        caster.kitAnimationLoops = false;
        caster.PlayAnimation();
    }
}

void StopMissileSound(SpellCast &cast) {
    if (cast.missileSound != nullptr) {
        Sound::Play::Stop(cast.missileSound, kSoundFadeSec);
        cast.missileSound = nullptr;
    }
}

void ReleaseMissile(SpellCast &cast) {
    StopMissileSound(cast);
    if (cast.missile != nullptr) {
        M2::SetAnimDoneCallback(cast.missile, nullptr, 0, 0);
        ReleaseLit(cast.missile, cast.light);
        cast.missile = nullptr;
    }
}

// The target's attachment point, or its position raised by 0.75 of its
// height. Unchanged while the target's model has not loaded.
// The attachment Launch settles (0x0061CEB0): the visual's, else 15, else
// 19, else none (-1).
int TargetAttachment(const SpellCast &cast) {
    const Actor *target = cast.target;
    if (target == nullptr || target->model == nullptr)
        return -1;
    const auto index = Game::Read<uint32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_MISSILE_ATTACHMENT);
    const uint32_t wanted =
        index < Offsets::SPELL_VISUAL_ATTACHMENT_COUNT
            ? Game::Read<uint32_t>(static_cast<uintptr_t>(Offsets::VAR_SPELL_VISUAL_ATTACHMENTS) +
                                   index * 4)
            : kFallbackAttachments[0];
    const uint32_t tries[] = {wanted, kFallbackAttachments[0], kFallbackAttachments[1]};
    for (uint32_t id : tries) {
        if (M2::HasAttachment(target->model, id))
            return static_cast<int>(id);
    }
    return -1;
}

void Aim(SpellCast &cast) {
    Actor *target = cast.target;
    if (target == nullptr || !ChainLoaded(target->model))
        return;
    if (const int id = TargetAttachment(cast); id >= 0) {
        M2::GetAttachmentPosition(target->model, static_cast<uint32_t>(id), cast.destination);
        return;
    }
    float box[6];
    M2::GetBoundingBox(target->model, box);
    cast.destination[0] = target->position[0];
    cast.destination[1] = target->position[1];
    cast.destination[2] = target->position[2] + box[5] * target->scale * kAimHeight;
}

// The spell reaches its target: the caster's OnSpellVisualImpact (queued),
// the wound animation, then the impact kit once.
void Impact(Actor &caster, SpellCast &cast, bool missileHit) {
    caster.spellImpacts.push_back({cast.id, cast.spellID, cast.target});
    Actor *target = cast.target;
    if (target == nullptr || target->model == nullptr)
        return;
    if (missileHit)
        target->PlayKitAnimation(kAnimCombatWound, false);
    PlayKit(*target, Kit(cast.visual, Offsets::OFF_SPELLVISUAL_IMPACT_KIT), false, nullptr);
}

// StickInUnit (0x0061E7C0), from ArriveHit (0x0061DC50): a missile shot from
// a bow, crossbow or thrown weapon stays in the target at the attachment it
// flew at, as it struck, playing Impact, for 5 s. The missile is snapped to
// the destination first (Arrive 0x0061E1D0), keeping the turn of its flight.
bool Stick(SpellCast &cast) {
    using Matrix_t = void(__thiscall *)(void *model, float *out, uint32_t id);
    Actor *target = cast.target;
    if (!cast.sticks || cast.attachment < 0 || cast.missile == nullptr || target == nullptr ||
        target->model == nullptr || !M2::HasAttachment(target->model, static_cast<uint32_t>(cast.attachment)))
        return false;
    void *missile = cast.missile;
    float *base = M2::BaseMatrix(missile);
    for (int i = 0; i < 3; ++i)
        base[12 + i] = cast.destination[i];
    M2::SetAnimDoneCallback(missile, nullptr, 0, 0);
    M2::PlayAnimation(missile, -1, Offsets::ANIM_IMPACT, -1, 0, 1.0f, 1, 1);
    M2::Attach(missile, target->model, static_cast<uint32_t>(cast.attachment));
    float attachment[16], inverse[16], world[16], local[16];
    reinterpret_cast<Matrix_t>(Offsets::FUN_M2MODEL_GET_ATTACHMENT_MATRIX)(target->model, attachment,
                                                                          static_cast<uint32_t>(cast.attachment));
    Gx::InvertAffine(attachment, inverse, target->scale);
    M2::GetBaseTransform(missile, world);
    Gx::Multiply(local, world, inverse);
    M2::SetBaseTransform(missile, local);
    // The world object goes (0x0061E859); the missile is lit as the target's
    // child.
    Model::DisplayInfo::LightChildren(target->SceneFrame(), target->model);
    cast.light.reset();
    target->stuckMissiles.push_back({missile, static_cast<float>(Offsets::MISSILE_STICK_MS) * 0.001f});
    cast.missile = nullptr;
    return true;
}

void LeaveImpactModel(Actor &caster, const SpellCast &cast) {
    const char *file = EffectFile(Game::Read<uint32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_IMPACT_MODEL));
    if (file == nullptr)
        return;
    void *model = M2::CreateModel(Model::Pane::Scene(caster.SceneFrame()), file);
    if (model == nullptr)
        return;
    std::unique_ptr<EntityLight> light;
    LightAsEntity(model, light, caster.SceneFrame());
    const float axisZ[3] = {0.0f, 0.0f, 1.0f};
    const float scales[3] = {1.0f, 1.0f, 1.0f};
    M2::SetBaseTRS(model, cast.destination, 0.0f, axisZ, scales);
    caster.impactModels.push_back({model, 0, std::move(light)});
}

// ReleaseMissilesFromHand's point: the first release event the model has,
// else the caster's position.
void HandPoint(Actor &caster, float (&point)[3]) {
    if (ChainLoaded(caster.model)) {
        for (uint32_t tag : kReleaseEvents) {
            if (M2::HasEvent(caster.model, tag)) {
                M2::GetEventPosition(caster.model, tag, point);
                return;
            }
        }
    }
    for (int i = 0; i < 3; ++i)
        point[i] = caster.position[i];
}

// CMissile::Launch: aims, fits the speed to the time spent queued, plays
// InFlight and starts the flight sound. False while the model loads.
bool Launch(Actor &caster, SpellCast &cast, const float (&from)[3]) {
    if (!M2::IsLoaded(cast.missile, 0, 0))
        return false;
    for (int i = 0; i < 3; ++i)
        cast.position[i] = from[i];
    cast.attachment = ChainLoaded(cast.target->model) ? TargetAttachment(cast) : -1;
    Aim(cast);
    const float dx = cast.destination[0] - from[0];
    const float dy = cast.destination[1] - from[1];
    const float dz = cast.destination[2] - from[2];
    const float distance = std::sqrt(dz * dz + dy * dy + dx * dx);
    const float flightSec = distance / cast.speed - cast.queuedSec;
    cast.speed = flightSec > 0.0f ? (std::fmin)(distance / flightSec, kMaxSpeed) : kMaxSpeed;
    M2::SetAnimDoneCallback(cast.missile, Model::VisualKit::LoopCallback(), 0, 0);
    M2::PlayAnimation(cast.missile, -1, flightSec > 0.0f ? kAnimInFlight : kAnimStand, 0, 0, 1.0f, 1,
                      1);
    cast.missileSound = Sounds::Loop(caster.SceneFrame(),
                                     Game::Read<int32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_MISSILE_SOUND),
                                     cast.position);
    cast.phase = SpellCast::Phase::Flight;
    return true;
}

// The launcher's model (0x0060A3D0): the visual's missile model, else the
// ammo's, else the error cube. A shot made during a rifle animation is drawn
// at alpha 0 (the bullet is not seen).
void *MakeMissile(Actor &caster, const SpellCast &cast) {
    void *scene = Model::Pane::Scene(caster.SceneFrame());
    void *model = nullptr;
    const auto effect = Game::Read<int32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_MISSILE_MODEL);
    if (effect < 1) {
        if (cast.ammoDisplay != 0)
            model = LooseItemModel(caster.model, cast.ammoDisplay,
                                   cast.ammoType == Offsets::INVTYPE_THROWN ? Offsets::LOOSE_ITEM_WEAPON
                                                                            : Offsets::LOOSE_ITEM_AMMO);
    } else if (const char *file = EffectFile(static_cast<uint32_t>(effect))) {
        model = M2::CreateModel(scene, file);
    }
    if (model == nullptr)
        model = M2::CreateModel(scene, kErrorCube);
    if (model != nullptr && M2::IsRifleAnimation(caster.CurrentAnimation()))
        M2::SetAlpha(model, 0.0f);
    return model;
}

void StartCast(Actor &caster, SpellCast &cast) {
    EndPrecast(caster, cast);
    cast.phase = SpellCast::Phase::Cast;
    PlayKit(caster, Kit(cast.visual, Offsets::OFF_SPELLVISUAL_CAST_KIT), false, nullptr);
    const auto *spell = DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT,
                                    cast.spellID);
    cast.speed = spell != nullptr ? Game::Read<float>(spell, Offsets::OFF_SPELL_RECORD_SPEED) : 0.0f;
    const bool missile = (Game::Read<uint32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_HAS_MISSILE) != 0 ||
                          cast.ammoDisplay != 0) &&
                         cast.speed > 0.0f && cast.target != &caster;
    if (missile) {
        cast.missile = MakeMissile(caster, cast);
        cast.sticks = spell != nullptr && IsRanged(spell) && IsBowCrossbowOrThrown(caster.ranged);
    }
    if (cast.missile != nullptr) {
        LightAsEntity(cast.missile, cast.light, caster.SceneFrame());
        Aim(cast);
        return;
    }
    Impact(caster, cast, false);
}

// Moves the cast on; true once it is over.
bool Step(Actor &caster, SpellCast &cast, float elapsedSec) {
    switch (cast.phase) {
    case SpellCast::Phase::Precast:
        cast.precastLeftSec -= elapsedSec;
        if (cast.precastLeftSec > 0.0f)
            return false;
        StartCast(caster, cast);
        return cast.missile == nullptr;
    case SpellCast::Phase::Cast: {
        cast.queuedSec += elapsedSec;
        float from[3];
        if (caster.releaseEvent) {
            for (int i = 0; i < 3; ++i)
                from[i] = caster.releasePoint[i];
        } else if (KitAnimation(Kit(cast.visual, Offsets::OFF_SPELLVISUAL_CAST_KIT)) <= 0 ||
                   caster.kitAnimationEnded || cast.queuedSec >= kReleaseTimeoutSec) {
            HandPoint(caster, from);
        } else {
            return false;
        }
        // ReleaseMissiles ends the ranged pose (0x0060C951).
        if (Launch(caster, cast, from))
            EndRangedPose(caster);
        return false;
    }
    case SpellCast::Phase::Flight: {
        Aim(cast);
        float d[3];
        float lengthSq = 0.0f;
        for (int i = 0; i < 3; ++i) {
            d[i] = cast.destination[i] - cast.position[i];
            lengthSq += d[i] * d[i];
        }
        const float step = elapsedSec * cast.speed;
        if (step * step > lengthSq) {
            for (int i = 0; i < 3; ++i)
                cast.position[i] = cast.destination[i];
            StopMissileSound(cast);
            LeaveImpactModel(caster, cast);
            Impact(caster, cast, true);
            if (!Stick(cast))
                ReleaseMissile(cast);
            return true;
        }
        const float scale = step / std::sqrt(lengthSq);
        for (int i = 0; i < 3; ++i)
            cast.position[i] += d[i] * scale;
        return false;
    }
    }
    return true;
}

// A cast past its precast already ended it: the caster's looping animation
// now belongs to a newer cast's precast, which a missile landing does not end.
void DropCast(Actor &caster, SpellCast &cast) {
    if (cast.phase == SpellCast::Phase::Precast)
        EndPrecast(caster, cast);
    ReleaseMissile(cast);
}

} // namespace

// An interrupt ends a spell still preparing (0x0060D1C0 stops its kits: the
// effects, the tint and the looping animation; the failed spell's EndPrecast
// 0x0060FC50 ends the ranged pose); a spell already cast goes on, its missile
// included.
void Stop(Actor &caster) {
    bool stopped = false;
    for (size_t i = 0; i < caster.spellCasts.size();) {
        if (caster.spellCasts[i].phase != SpellCast::Phase::Precast) {
            ++i;
            continue;
        }
        DropCast(caster, caster.spellCasts[i]);
        caster.spellCasts.erase(caster.spellCasts.begin() + static_cast<ptrdiff_t>(i));
        stopped = true;
    }
    if (stopped)
        EndRangedPose(caster);
}

// $BWP (OnCombatModelEvent 0x006247D0): with a ranged spell under way, the
// weapon draws and the ammo goes on the string unless a gun or crossbow
// shoots it.
void BowPull(Actor &caster) {
    if (!CastsRanged(caster))
        return;
    PlayBowPull(caster);
    if (!KeepsAmmoOff(caster) && caster.heldAmmo != nullptr && M2::Parent(caster.heldAmmo) == nullptr)
        Nock(caster);
    caster.bowDrawn = true;
}

// $BWR (0x00600159): the bow is no longer drawn; with a missile waiting, a
// bow animation plays the weapon's stand and a rifle animation its release;
// the ammo leaves the string.
void BowRelease(Actor &caster) {
    caster.bowDrawn = false;
    if (MissileQueued(caster) && caster.model != nullptr) {
        const int anim = caster.CurrentAnimation();
        void *ranged = RangedModel(caster);
        if (M2::IsBowAnimation(anim) &&
            M2::HasAttachment(caster.model, Offsets::ATTACH_BOW_RELEASE)) {
            if (ranged != nullptr)
                M2::PlayAnimation(ranged, -1, Offsets::ANIM_STAND, -1, 0, 1.0f, 1, 1);
        } else if (M2::IsRifleAnimation(anim) &&
                   M2::HasAttachment(caster.model, Offsets::ATTACH_RIFLE_RELEASE)) {
            if (ranged != nullptr)
                M2::PlayAnimation(ranged, -1, Offsets::ANIM_BOW_RELEASE, -1, 0, 1.0f, 1, 1);
        }
    }
    if (caster.heldAmmo != nullptr && M2::Parent(caster.heldAmmo) != nullptr)
        M2::Detach(caster.heldAmmo);
}

// The sequence-end handler's ranged cases (0x005FC3F0): a load goes on to
// its hold; a hold plays again while the weapon is held and the spell still
// prepares.
int RangedAnimationAfter(const Actor &caster, int ended) {
    switch (ended) {
    case Offsets::ANIM_LOAD_BOW:
        return Offsets::ANIM_HOLD_BOW;
    case Offsets::ANIM_LOAD_RIFLE:
        return Offsets::ANIM_HOLD_RIFLE;
    case Offsets::ANIM_HOLD_BOW:
    case Offsets::ANIM_HOLD_RIFLE:
    case Offsets::ANIM_HOLD_THROWN:
    case Offsets::ANIM_LOAD_THROWN:
        break;
    default:
        return -1;
    }
    bool preparing = false;
    for (const SpellCast &cast : caster.spellCasts)
        preparing = preparing || cast.phase == SpellCast::Phase::Precast;
    if (RangedModel(caster) == nullptr || !preparing)
        return -1;
    return ended == Offsets::ANIM_LOAD_THROWN ? static_cast<int>(Offsets::ANIM_HOLD_THROWN) : ended;
}

uint32_t Play(Actor &caster, uint32_t spellID, Actor *target, int castTimeMs) {
    using GetCastTime_t = int(__fastcall *)(int spellID, int pet, int allowNegative);
    static uint32_t s_lastCastID = 0;
    const uint8_t *spell =
        DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT, spellID);
    if (spell == nullptr || caster.model == nullptr)
        return 0;
    uint8_t visual[Offsets::SPELL_VISUAL_SIZE];
    if (!BuildVisual(caster, spell, visual))
        return 0;
    // A new cast ends the precast of one still preparing.
    Stop(caster);
    if (castTimeMs < 0)
        castTimeMs = reinterpret_cast<GetCastTime_t>(Offsets::FUN_GET_CAST_TIME)(
            static_cast<int>(spellID), 0, 0);
    SpellCast cast;
    if (++s_lastCastID == 0) // the IDs start again from 1 after 2^32 casts
        s_lastCastID = 1;
    cast.id = s_lastCastID;
    cast.spellID = spellID;
    std::memcpy(cast.visual, visual, sizeof visual);
    ShotAmmo(caster, spell, cast.ammoDisplay, cast.ammoType);
    // HandleSpellStart (0x006E78A1): a ranged or auto-repeat spell sets the
    // ammo in hand.
    if (IsRanged(spell) || (Game::Read<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2) &
                            Offsets::SPELL_ATTR_EX2_AUTOREPEAT_FLAG) != 0) {
        HoldAmmo(caster, cast.ammoDisplay);
        caster.DrawRanged();
    }
    cast.target = target != nullptr ? target : &caster;
    caster.spellCasts.push_back(std::move(cast));
    SpellCast &added = caster.spellCasts.back();
    const uint8_t *precast = Kit(visual, Offsets::OFF_SPELLVISUAL_PRECAST_KIT);
    if (castTimeMs > 0 && precast != nullptr) {
        added.phase = SpellCast::Phase::Precast;
        added.precastLeftSec = static_cast<float>(castTimeMs) * 0.001f;
        added.precastTinted = HasTintProcedure(precast);
        PlayKit(caster, precast, true, &added.precastEffects);
        return added.id;
    }
    StartCast(caster, added);
    const uint32_t id = added.id;
    if (added.missile == nullptr)
        caster.spellCasts.pop_back();
    return id;
}

void Update(Actor &caster, float elapsedSec) {
    if (caster.paused || caster.globalPaused)
        elapsedSec = 0.0f;
    // By index: a step may start a cast's impact on this caster, which plays
    // its kit, not another cast.
    for (size_t i = 0; i < caster.spellCasts.size();) {
        if (!Step(caster, caster.spellCasts[i], elapsedSec)) {
            ++i;
            continue;
        }
        DropCast(caster, caster.spellCasts[i]);
        caster.spellCasts.erase(caster.spellCasts.begin() + static_cast<ptrdiff_t>(i));
    }
    caster.releaseEvent = false;
    caster.kitAnimationEnded = false;
    for (SpellCast &cast : caster.spellCasts)
        if (cast.phase == SpellCast::Phase::Flight)
            SettleLight(cast.missile, cast.light.get(), elapsedSec);
    for (ImpactModel &impact : caster.impactModels)
        SettleLight(impact.model, impact.light.get(), elapsedSec);
    for (size_t i = 0; i < caster.stuckMissiles.size();) {
        StuckMissile &stuck = caster.stuckMissiles[i];
        stuck.leftSec -= elapsedSec;
        if (stuck.leftSec > 0.0f) {
            ++i;
            continue;
        }
        FreeStuck(stuck);
        caster.stuckMissiles.erase(caster.stuckMissiles.begin() + static_cast<ptrdiff_t>(i));
    }
    for (size_t i = 0; i < caster.impactModels.size();) {
        ImpactModel &impact = caster.impactModels[i];
        const bool gone = impact.framesShown >= 2 && !M2::HasLiveParticles(impact.model);
        if (!gone) {
            ++i;
            continue;
        }
        ReleaseLit(impact.model, impact.light);
        caster.impactModels.erase(caster.impactModels.begin() + static_cast<ptrdiff_t>(i));
    }
}

void Link(Actor &caster) {
    for (SpellCast &cast : caster.spellCasts) {
        if (cast.phase != SpellCast::Phase::Flight || !M2::IsReady(cast.missile, 0, 1))
            continue;
        M2::SetAnimated(cast.missile, true);
        M2::SetVisible(cast.missile, true);
    }
    for (ImpactModel &impact : caster.impactModels) {
        if (!M2::IsReady(impact.model, 0, 1))
            continue;
        M2::SetAnimated(impact.model, true);
        M2::SetVisible(impact.model, true);
        ++impact.framesShown;
    }
}

// The missile faces its travel: yaw about the up axis, then pitch, as an
// actor's placement turns.
void Place(Actor &caster) {
    const float axisY[3] = {0.0f, 1.0f, 0.0f};
    const float axisZ[3] = {0.0f, 0.0f, 1.0f};
    const float scales[3] = {1.0f, 1.0f, 1.0f};
    for (SpellCast &cast : caster.spellCasts) {
        if (cast.phase != SpellCast::Phase::Flight)
            continue;
        const float dx = cast.destination[0] - cast.position[0];
        const float dy = cast.destination[1] - cast.position[1];
        const float dz = cast.destination[2] - cast.position[2];
        const float yaw = std::atan2(dy, dx);
        const float pitch = -std::atan2(dz, std::sqrt(dx * dx + dy * dy));
        M2::SetBaseTRS(cast.missile, cast.position, yaw, axisZ, scales);
        Gx::Rotate(M2::BaseMatrix(cast.missile), pitch, axisY);
        Sounds::Move(caster.SceneFrame(), cast.missileSound, cast.position);
    }
}

void Silence(Actor &caster) {
    for (SpellCast &cast : caster.spellCasts)
        if (cast.missileSound != nullptr) {
            Sound::Play::Stop(cast.missileSound, 0.0f);
            cast.missileSound = nullptr;
        }
}

void Drop(Actor &caster) {
    for (SpellCast &cast : caster.spellCasts)
        DropCast(caster, cast);
    caster.spellCasts.clear();
    for (ImpactModel &impact : caster.impactModels)
        ReleaseLit(impact.model, impact.light);
    caster.impactModels.clear();
    for (StuckMissile &stuck : caster.stuckMissiles)
        FreeStuck(stuck);
    caster.stuckMissiles.clear();
    caster.releaseEvent = false;
    HoldAmmo(caster, 0);
    caster.bowDrawn = false;
}

} // namespace Model::Scene::Casts
