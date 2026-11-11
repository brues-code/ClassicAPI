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

// `actor:AddAuraVisual(spellID)` / `actor:RemoveAuraVisual([spellID])`, a
// ClassicAPI extension: the auras an actor carries, shown as the world shows
// a unit's. A unit shows each aura on it at once, where SetSpellVisualKit
// holds one kit:
//   - A new aura (0x005FF350 shows an aura slot): the state kit of the
//     spell's visual (+0x10) plays as a lasting kit (kind 2, 0x0060EDF0).
//     Each effect column's model hangs at its attachment and holds 158 Hold
//     at each end (KitEffectHoldCallback 0x005FF170). The +0x30 world effect
//     is planted where the unit stands (PlantEffect 0x0061FCF0), turned by
//     its facing and scaled by its scale (0x00620A90), and it follows the
//     unit when the spell roots (aura 26: flag 0x4000, re-placed at the
//     owner each update, 0x006205A5). The kit's tint and alpha apply.
//   - The aura's end (0x005FF290 clears the slot and stops the spell's kits
//     with force, 0x00614150): an effect whose model is loaded and has 159
//     Decay plays it and stops at its end; any other stops at once
//     (CEffect::Stop 0x006203E0: hidden, its particles finish).
// One aura slot holds a spell, so each spell shows once. The auras outlive
// a model change: the new model shows them once it loads, as
// SetSpellVisualKit's kit. ClearModel ends them, as the pool releases an
// actor through it.
//
// The state kit's animation is pinned (ApplyEffectAnimation 0x005FDE80,
// Actor::PlayAnimation): while the actor neither moves nor swims, every
// animation but a death plays the newest aura's kit animation instead, so a
// stunned actor stands stunned. An aura's end does not pick again; its
// animation plays on until its sequence ends, as on a unit.
//
// The kit's sound plays once per play at the actor (the first effect's arm
// pass, 0x00620C10 to 0x006210B9), or loops on it when its definition has
// bit 0x200 (AddEffectSound 0x0061FEC0) until the aura's end stops it with
// CEffect::Stop's fade. A definition with no-duplicates (0x20), as Frost
// Nova's and Fear's, plays once however many actors show it at once.
//
// What the world does and this does not: the auras' priorities and visions
// (a scene shows every aura it is given).

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/M2.h"
#include "model/VisualKit.h"
#include "sound/Play.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Model::Scene::AuraVisuals {
namespace {

const uint8_t *Spell(uint32_t spellID) {
    return DBC::Record(Offsets::VAR_SPELL_RECORDS, Offsets::VAR_SPELL_RECORD_COUNT, spellID);
}

// The state kit of the spell's visual (GetSpellVisualRecord), or null.
const uint8_t *StateKit(const uint8_t *spell) {
    using GetVisual_t = const uint8_t *(__fastcall *)(const uint8_t *spell);
    const uint8_t *visual = reinterpret_cast<GetVisual_t>(Offsets::FUN_GET_SPELL_VISUAL)(spell);
    return visual != nullptr
               ? Model::VisualKit::Record(Game::Read<uint32_t>(visual, Offsets::OFF_SPELLVISUAL_STATE_KIT))
               : nullptr;
}

// A spell with a root aura in one of its effects: its world effect follows
// the unit (flag 0x4000).
bool Roots(const uint8_t *spell) {
    for (int i = 0; i < 3; ++i) {
        if (Game::Read<int32_t>(spell, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME + i * 4) ==
            Offsets::SPELL_AURA_MOD_ROOT)
            return true;
    }
    return false;
}

void ShowOne(Actor &actor, AuraVisual &aura) {
    if (aura.shown || actor.model == nullptr || !actor.loaded)
        return;
    aura.shown = true;
    void *frame = actor.SceneFrame();
    Model::VisualKit::Apply(frame, actor.model, aura.kit, false, &aura.effects);
    Shakes::PlayKit(actor, aura.kit);
    // The kit's sound, once per play: a looping one stays on the unit until
    // the aura's effects stop.
    const auto sound = Game::Read<int32_t>(aura.kit, Offsets::OFF_SPELLVISUALKIT_SOUND);
    if (Sounds::KitLoops(sound)) {
        aura.loopSoundID = sound;
        aura.loopSound = Sounds::Loop(frame, sound, actor.position);
    } else if (sound > 0) {
        Sounds::Play(frame, sound, actor.position);
    }
    const uint8_t *effect =
        DBC::Record(Offsets::VAR_SPELLVISUALEFFECTNAME_RECORDS, Offsets::VAR_SPELLVISUALEFFECTNAME_COUNT,
                    Game::Read<uint32_t>(aura.kit, Offsets::OFF_SPELLVISUALKIT_WORLD_EFFECT));
    if (effect == nullptr)
        return;
    void *planted =
        M2::CreateModel(Model::Pane::Scene(frame), Game::Read<const char *>(effect, Offsets::OFF_SPELLVISUALEFFECTNAME_FILE));
    if (planted == nullptr)
        return;
    aura.worldEffect = planted;
    for (int i = 0; i < 3; ++i)
        aura.worldPosition[i] = actor.position[i];
    aura.worldYaw = actor.ShownYaw();
    aura.worldScale = actor.scale > 0.0f ? actor.scale : 1.0f;
    LightAsEntity(planted, aura.worldLight, frame);
    M2::SetAnimDoneCallback(planted, Model::VisualKit::LoopCallback(), 0, 0);
}

// The aura's effects go: stopped as its end stops them, or at once with the
// model.
void HideOne(Actor &actor, AuraVisual &aura, bool stop) {
    void *frame = actor.SceneFrame();
    if (aura.loopSound != nullptr)
        Sound::Play::Stop(aura.loopSound, stop ? Sounds::kStopFadeSec : 0.0f);
    aura.loopSound = nullptr;
    aura.loopSoundID = 0;
    for (void *effect : aura.effects) {
        if (stop) {
            Model::VisualKit::Stop(frame, effect);
        } else {
            M2::Detach(effect);
            M2::Release(effect);
        }
    }
    aura.effects.clear();
    if (aura.worldEffect != nullptr) {
        if (stop) {
            Casts::Retire(actor, aura.worldEffect, std::move(aura.worldLight));
        } else {
            M2::SetAnimDoneCallback(aura.worldEffect, nullptr, 0, 0);
            ReleaseLit(aura.worldEffect, aura.worldLight);
        }
        aura.worldEffect = nullptr;
    }
    aura.shown = false;
}

} // namespace

bool Add(Actor &actor, uint32_t spellID) {
    for (const AuraVisual &aura : actor.auraVisuals) {
        if (aura.spellID == spellID)
            return true;
    }
    const uint8_t *spell = Spell(spellID);
    const uint8_t *kit = spell != nullptr ? StateKit(spell) : nullptr;
    if (kit == nullptr)
        return false;
    AuraVisual aura;
    aura.spellID = spellID;
    aura.kit = kit;
    aura.worldFollows = Roots(spell);
    actor.auraVisuals.push_back(std::move(aura));
    ShowOne(actor, actor.auraVisuals.back());
    // A state kit with an animation picks the base animation again unless
    // the unit already plays it (0x0060F383 to 0x0060F399); the pick takes
    // the pin.
    if (actor.model != nullptr && actor.loaded) {
        const int pinned = PinnedAnimation(actor);
        if (pinned > 0 && M2::GetAnimState(actor.model, -1).id != static_cast<uint32_t>(pinned))
            actor.PlayAnimation();
    }
    return true;
}

void Remove(Actor &actor, uint32_t spellID) {
    bool colored = false;
    for (size_t i = 0; i < actor.auraVisuals.size();) {
        AuraVisual &aura = actor.auraVisuals[i];
        if (spellID != 0 && aura.spellID != spellID) {
            ++i;
            continue;
        }
        colored = colored || (aura.shown && Model::VisualKit::SetsColor(aura.kit));
        HideOne(actor, aura, true);
        actor.auraVisuals.erase(actor.auraVisuals.begin() + static_cast<ptrdiff_t>(i));
    }
    // The aura's tint goes: the model's own color, then the kits still on it.
    if (colored && actor.model != nullptr) {
        const float white[3] = {1.0f, 1.0f, 1.0f};
        M2::SetDiffuse(actor.model, white);
        actor.ApplyAlpha();
        if (const uint8_t *kit = Model::VisualKit::Record(actor.spellVisualKit))
            Model::VisualKit::ApplyProcedures(actor.model, kit);
        ApplyColors(actor);
    }
}

void Show(Actor &actor) {
    for (AuraVisual &aura : actor.auraVisuals)
        ShowOne(actor, aura);
}

void Drop(Actor &actor) {
    for (AuraVisual &aura : actor.auraVisuals)
        HideOne(actor, aura, false);
}

int PinnedAnimation(const Actor &actor) {
    for (auto it = actor.auraVisuals.rbegin(); it != actor.auraVisuals.rend(); ++it) {
        if (!it->shown)
            continue;
        const auto anim = Game::Read<int32_t>(it->kit, Offsets::OFF_SPELLVISUALKIT_ANIMATION);
        if (anim > 0)
            return anim;
    }
    return -1;
}

void ApplyColors(Actor &actor) {
    if (actor.model == nullptr)
        return;
    for (const AuraVisual &aura : actor.auraVisuals) {
        if (aura.shown)
            Model::VisualKit::ApplyProcedures(actor.model, aura.kit);
    }
}

// The world has no pause; a paused scene (or actor) silences the loops, which
// start again when the pause ends, as a hidden scene's do when it shows.
void Update(Actor &actor, float elapsedSec) {
    const bool held = IsPaused(actor.SceneFrame()) || actor.paused || actor.globalPaused;
    for (AuraVisual &aura : actor.auraVisuals) {
        if (aura.worldEffect != nullptr)
            SettleLight(aura.worldEffect, aura.worldLight.get(), elapsedSec);
        if (aura.loopSoundID == 0)
            continue;
        if (held && aura.loopSound != nullptr) {
            Sound::Play::Stop(aura.loopSound, 0.0f);
            aura.loopSound = nullptr;
        } else if (!held && aura.loopSound == nullptr) {
            aura.loopSound = Sounds::Loop(actor.SceneFrame(), aura.loopSoundID, actor.position);
        }
    }
}

void Silence(Actor &actor) {
    for (AuraVisual &aura : actor.auraVisuals) {
        if (aura.loopSound != nullptr) {
            Sound::Play::Stop(aura.loopSound, 0.0f);
            aura.loopSound = nullptr;
        }
    }
}

void Link(Actor &actor) {
    for (AuraVisual &aura : actor.auraVisuals) {
        if (aura.worldEffect == nullptr || !M2::IsReady(aura.worldEffect, 0, 1))
            continue;
        M2::SetVisible(aura.worldEffect, actor.shown);
        M2::SetAnimated(aura.worldEffect, true);
    }
}

void Place(Actor &actor) {
    const float axisZ[3] = {0.0f, 0.0f, 1.0f};
    for (AuraVisual &aura : actor.auraVisuals) {
        Sounds::Move(actor.SceneFrame(), aura.loopSound, actor.position);
        if (aura.worldEffect == nullptr)
            continue;
        if (aura.worldFollows) {
            for (int i = 0; i < 3; ++i)
                aura.worldPosition[i] = actor.position[i];
        }
        const float scales[3] = {aura.worldScale, aura.worldScale, aura.worldScale};
        M2::SetBaseTRS(aura.worldEffect, aura.worldPosition, aura.worldYaw, axisZ, scales);
    }
}

} // namespace Model::Scene::AuraVisuals
