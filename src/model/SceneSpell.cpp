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
// reflections (no combat), arrows stuck in the target, and the positional
// 3D sound (a UI frame plays its sounds 2D, like PlaySound).
//
// A paused caster (SetPaused) holds its casts and missiles where they are.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/M2.h"
#include "model/VisualKit.h"
#include "sound/Play.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace Model::Scene::Casts {

namespace {

// The release events, '$CSL', '$CSR', '$CST'.
constexpr uint32_t kReleaseEvents[] = {0x4C534324, 0x52534324, 0x54534324};
constexpr int kAnimStand = 0;
constexpr int kAnimCombatWound = 9;
constexpr int kAnimInFlight = 144;
constexpr float kReleaseTimeoutSec = 2.0f; // 0x0060A814
constexpr float kMaxSpeed = 100.0f;        // 0x0080C854
constexpr float kSoundFadeSec = 0.15f;     // the missile's free, 0x0061CD70
constexpr float kAimHeight = 0.75f;        // 0x008012CC
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
    Sound::Play::PlayKitID(Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_SOUND));
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
    if (caster.kitAnimationLoops) {
        caster.kitAnimation = -1;
        caster.kitAnimationLoops = false;
        caster.PlayAnimation();
    }
}

void ReleaseMissile(SpellCast &cast) {
    if (cast.missileSound != nullptr) {
        Sound::Play::Stop(cast.missileSound, kSoundFadeSec);
        cast.missileSound = nullptr;
    }
    if (cast.missile != nullptr) {
        M2::SetAnimDoneCallback(cast.missile, nullptr, 0, 0);
        M2::SetVisible(cast.missile, false);
        M2::Release(cast.missile);
        cast.missile = nullptr;
    }
}

// The target's attachment point, or its position raised by 0.75 of its
// height. Unchanged while the target's model has not loaded.
void Aim(SpellCast &cast) {
    Actor *target = cast.target;
    if (target == nullptr || !ChainLoaded(target->model))
        return;
    const auto index = Game::Read<uint32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_MISSILE_ATTACHMENT);
    const uint32_t wanted =
        index < Offsets::SPELL_VISUAL_ATTACHMENT_COUNT
            ? Game::Read<uint32_t>(static_cast<uintptr_t>(Offsets::VAR_SPELL_VISUAL_ATTACHMENTS) +
                                   index * 4)
            : kFallbackAttachments[0];
    const uint32_t tries[] = {wanted, kFallbackAttachments[0], kFallbackAttachments[1]};
    for (uint32_t id : tries) {
        if (M2::HasAttachment(target->model, id)) {
            M2::GetAttachmentPosition(target->model, id, cast.destination);
            return;
        }
    }
    float box[6];
    M2::GetBoundingBox(target->model, box);
    cast.destination[0] = target->position[0];
    cast.destination[1] = target->position[1];
    cast.destination[2] = target->position[2] + box[5] * target->scale * kAimHeight;
}

// The wound animation, then the impact kit once.
void Impact(SpellCast &cast, bool missileHit) {
    Actor *target = cast.target;
    if (target == nullptr || target->model == nullptr)
        return;
    if (missileHit)
        target->PlayKitAnimation(kAnimCombatWound, false);
    PlayKit(*target, Kit(cast.visual, Offsets::OFF_SPELLVISUAL_IMPACT_KIT), false, nullptr);
}

void LeaveImpactModel(Actor &caster, const SpellCast &cast) {
    const char *file = EffectFile(Game::Read<uint32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_IMPACT_MODEL));
    if (file == nullptr)
        return;
    void *model = M2::CreateModel(Model::Pane::Scene(caster.SceneFrame()), file);
    if (model == nullptr)
        return;
    M2::SetRenderCallback(model, reinterpret_cast<const void *>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB),
                          caster.SceneFrame());
    const float axisZ[3] = {0.0f, 0.0f, 1.0f};
    const float scales[3] = {1.0f, 1.0f, 1.0f};
    using SetTRS_t = void(__thiscall *)(void *, const float *, float, const float *, const float *);
    reinterpret_cast<SetTRS_t>(Offsets::FUN_M2MODEL_SET_BASE_TRS)(model, cast.destination, 0.0f,
                                                                  axisZ, scales);
    caster.impactModels.push_back({model, 0});
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
    cast.missileSound = Sound::Play::PlayKitLooped(
        Game::Read<int32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_MISSILE_SOUND));
    cast.phase = SpellCast::Phase::Flight;
    return true;
}

void StartCast(Actor &caster, SpellCast &cast) {
    EndPrecast(caster, cast);
    cast.phase = SpellCast::Phase::Cast;
    PlayKit(caster, Kit(cast.visual, Offsets::OFF_SPELLVISUAL_CAST_KIT), false, nullptr);
    const auto *spell = DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT,
                                    cast.spellID);
    cast.speed = spell != nullptr ? Game::Read<float>(spell, Offsets::OFF_SPELL_RECORD_SPEED) : 0.0f;
    const bool missile = Game::Read<uint32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_HAS_MISSILE) != 0 &&
                         cast.speed > 0.0f && cast.target != &caster;
    if (missile) {
        const char *file =
            EffectFile(Game::Read<uint32_t>(cast.visual, Offsets::OFF_SPELLVISUAL_MISSILE_MODEL));
        void *scene = Model::Pane::Scene(caster.SceneFrame());
        cast.missile = M2::CreateModel(scene, file != nullptr ? file : kErrorCube);
        if (cast.missile == nullptr)
            cast.missile = M2::CreateModel(scene, kErrorCube);
    }
    if (cast.missile != nullptr) {
        M2::SetRenderCallback(cast.missile,
                              reinterpret_cast<const void *>(Offsets::FUN_SIMPLEMODELFFX_LIGHT_FOG_CB),
                              caster.SceneFrame());
        Aim(cast);
        return;
    }
    Impact(cast, false);
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
        Launch(caster, cast, from);
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
            ReleaseMissile(cast);
            LeaveImpactModel(caster, cast);
            Impact(cast, true);
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

void DropCast(Actor &caster, SpellCast &cast) {
    EndPrecast(caster, cast);
    ReleaseMissile(cast);
}

} // namespace

bool Play(Actor &caster, uint32_t spellID, Actor *target, int castTimeMs) {
    using GetVisual_t = const uint8_t *(__fastcall *)(const uint8_t *spell);
    using GetCastTime_t = int(__fastcall *)(int spellID, int pet, int allowNegative);
    const uint8_t *spell =
        DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT, spellID);
    if (spell == nullptr || caster.model == nullptr)
        return false;
    const uint8_t *visual = reinterpret_cast<GetVisual_t>(Offsets::FUN_GET_SPELL_VISUAL)(spell);
    if (visual == nullptr)
        return false;
    // A new cast ends the precast of one still preparing (0x0060D1C0 stops
    // the old spell's kits); missiles already out fly on.
    for (size_t i = 0; i < caster.spellCasts.size();) {
        if (caster.spellCasts[i].phase != SpellCast::Phase::Precast) {
            ++i;
            continue;
        }
        DropCast(caster, caster.spellCasts[i]);
        caster.spellCasts.erase(caster.spellCasts.begin() + static_cast<ptrdiff_t>(i));
    }
    if (castTimeMs < 0)
        castTimeMs = reinterpret_cast<GetCastTime_t>(Offsets::FUN_GET_CAST_TIME)(
            static_cast<int>(spellID), 0, 0);
    SpellCast cast;
    cast.spellID = spellID;
    cast.visual = visual;
    cast.target = target != nullptr ? target : &caster;
    caster.spellCasts.push_back(std::move(cast));
    SpellCast &added = caster.spellCasts.back();
    const uint8_t *precast = Kit(visual, Offsets::OFF_SPELLVISUAL_PRECAST_KIT);
    if (castTimeMs > 0 && precast != nullptr) {
        added.phase = SpellCast::Phase::Precast;
        added.precastLeftSec = static_cast<float>(castTimeMs) * 0.001f;
        added.precastTinted = HasTintProcedure(precast);
        PlayKit(caster, precast, true, &added.precastEffects);
        return true;
    }
    StartCast(caster, added);
    if (added.missile == nullptr)
        caster.spellCasts.pop_back();
    return true;
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
    for (size_t i = 0; i < caster.impactModels.size();) {
        ImpactModel &impact = caster.impactModels[i];
        const bool gone = impact.framesShown >= 2 && !M2::HasLiveParticles(impact.model);
        if (!gone) {
            ++i;
            continue;
        }
        M2::SetVisible(impact.model, false);
        M2::Release(impact.model);
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
    using SetTRS_t = void(__thiscall *)(void *, const float *, float, const float *, const float *);
    using Rotate_t = void(__thiscall *)(float *m, float angle, const float *axis, int);
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
        reinterpret_cast<SetTRS_t>(Offsets::FUN_M2MODEL_SET_BASE_TRS)(cast.missile, cast.position,
                                                                      yaw, axisZ, scales);
        reinterpret_cast<Rotate_t>(Offsets::FUN_MATRIX_ROTATE)(
            Game::Ptr<float>(cast.missile, Offsets::OFF_M2MODEL_BASE_MATRIX), pitch, axisY, 1);
    }
}

void Drop(Actor &caster) {
    for (SpellCast &cast : caster.spellCasts)
        DropCast(caster, cast);
    caster.spellCasts.clear();
    for (ImpactModel &impact : caster.impactModels) {
        M2::SetVisible(impact.model, false);
        M2::Release(impact.model);
    }
    caster.impactModels.clear();
    caster.releaseEvent = false;
}

} // namespace Model::Scene::Casts
