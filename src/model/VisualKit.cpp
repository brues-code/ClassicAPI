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

// `PlayerModel:ApplySpellVisualKit(spellVisualKitID [, oneShot])`: a spell's
// visual kit on the pane's model.
//
// The world plays a kit on a unit (0x0060EDF0) through effect objects, spell
// durations and the unit's own animation. A pane has none of these. The
// engine's own kit on a bare model is the char-select ghost
// (0x004727F0), and this is that routine for any kit, on the pane's model:
//   - Each of the nine effect columns: a model of the SpellVisualEffectName
//     file, made in the pane's scene and attached at the column's attachment
//     when the model has it (0x00472780). The children get the frame's light
//     like the item glows (Model::DisplayInfo::LightChildren).
//   - The procedures the ghost applies: kind 1 sets the tint, kind 14 the
//     alpha clamped to 0..1, as plain stores on the model.
// The kit's animation, sound and the world-only procedures are not played,
// as retail does not play them on a model frame either (probed: no cast
// pose, no sound; the tint applies).
//
// oneShot is the world's one-shot kind: CEffect::AddEffect (0x0061FDD0)
// gives the effect model a done callback, and EffectDoneCallback
// (0x005FBF50) stops the effect when the sequence ends cleanly (reason 0).
// CEffect::Stop (0x006203E0) clears the model's callbacks and hides it, so
// its emitters stop and the particles already out finish; each frame the
// drain (0x0061F680) releases a stopped effect once HasLiveParticles is
// false. Here the release is the detach from the pane's model, run on
// FrameTick's tail where no model is mid-draw. Without oneShot an effect
// loops until the model is replaced, as on retail: an effect model's sequence
// does not loop by itself, so it gets the world's loop callback
// (KitEffectLoopCallback, 0x0060ED00), which plays the ended animation again.
//
// The effects are children of the pane's model, so they go when the model
// does: SetUnit, SetItem, a hide and show, and so on. A model destroyed with
// stopped effects still on it detaches them first (0x0070E3F5). A stopped
// effect is in the pane's scene, which frees it with the frame, so the pane's
// teardown (Model::Teardown) drops our reference first.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "model/DisplayInfo.h"
#include "model/M2.h"
#include "model/Teardown.h"
#include "tick/FrameTick.h"

#include <cstdint>
#include <vector>

namespace Model::VisualKit {
namespace {

using ModelFn_t = void(__fastcall *)(void *model);
using HasAttachment_t = int(__thiscall *)(void *model, uint32_t id);
using Attach_t = void(__thiscall *)(void *child, void *parent, uint32_t id);
using SetAlpha_t = void(__thiscall *)(void *model, float alpha);
using SetDiffuse_t = void(__thiscall *)(void *model, const float *rgb);
using HasLiveParticles_t = int(__thiscall *)(void *model);

// The attachment of each effect column, in the ghost kit's order.
constexpr uint32_t kAttachments[] = {20, 34, 19, 21, 22, 17, 23, 24, 25};
constexpr int kProcedures = 4;
constexpr int32_t kProcTint = 1;
constexpr int32_t kProcAlpha = 14;
// The ghost kit's byte scale (0x008026C8).
constexpr float kByteScale = 1.0f / 255.0f;

// A stopped one-shot effect, with one reference of ours, and the pane whose
// scene holds it.
struct Stopped {
    void *pane;
    void *model;
};
std::vector<Stopped> g_stopped;

// EffectDoneCallback's test, then CEffect::Stop's model steps. The callback's
// user data is the pane.
void __fastcall OnEffectDone(void *model, uint32_t /*keyBone*/, uint32_t /*animID*/,
                             uint32_t reason, uint32_t /*overshoot*/, uint32_t userLow,
                             uint32_t /*userHigh*/) {
    if (reason != 0)
        return;
    M2::SetAnimDoneCallback(model, nullptr, 0, 0);
    M2::SetVisible(model, false);
    M2::AddRef(model);
    g_stopped.push_back({reinterpret_cast<void *>(static_cast<uintptr_t>(userLow)), model});
}

// KitEffectLoopCallback: a sequence that ended cleanly plays again.
void __fastcall OnEffectLoop(void *model, uint32_t /*keyBone*/, uint32_t animID, uint32_t reason,
                             uint32_t /*overshoot*/, uint32_t /*userLow*/,
                             uint32_t /*userHigh*/) {
    if (reason != 0)
        return;
    M2::PlayAnimation(model, -1, animID, -1, 0, 1.0f, 1, 1);
}

// Detaches a stopped effect that is still on its model, then drops our
// reference.
void Discard(void *model) {
    if (Game::Read<void *>(model, Offsets::OFF_M2MODEL_PARENT) != nullptr)
        reinterpret_cast<ModelFn_t>(Offsets::FUN_M2MODEL_DETACH)(model);
    M2::Release(model);
}

// Discards every stopped effect `done` picks.
template <typename Pred>
void DiscardWhere(Pred done) {
    for (size_t i = 0; i < g_stopped.size();) {
        if (!done(g_stopped[i])) {
            ++i;
            continue;
        }
        Discard(g_stopped[i].model);
        g_stopped[i] = g_stopped.back();
        g_stopped.pop_back();
    }
}

// The dying-effect drain: a stopped effect goes once its particles are gone,
// or at once when its model has already dropped it.
void Drain() {
    DiscardWhere([](const Stopped &e) {
        return Game::Read<void *>(e.model, Offsets::OFF_M2MODEL_PARENT) == nullptr ||
               !reinterpret_cast<HasLiveParticles_t>(Offsets::FUN_M2MODEL_HAS_LIVE_PARTICLES)(
                   e.model);
    });
}

const Tick::FrameTick::AutoSubscribe g_drain{&Drain};

// The pane's scene is about to free its stopped effects.
void OnPaneDestroyed(void *pane) {
    DiscardWhere([pane](const Stopped &e) { return e.pane == pane; });
}

const Model::Teardown::AutoSubscribe _teardown{&OnPaneDestroyed};

// The ghost kit's procedure step: kind 1 tints, kind 14 sets the alpha.
void ApplyProcedures(void *model, const uint8_t *kit) {
    for (int i = 0; i < kProcedures; ++i) {
        const auto kind = Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_KIND + i * 4);
        const float value = Game::Read<float>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_VALUE + i * 4);
        if (kind == kProcTint) {
            const auto color = static_cast<uint32_t>(static_cast<int32_t>(value));
            const float rgb[3] = {static_cast<float>((color >> 16) & 0xFF) * kByteScale,
                                  static_cast<float>((color >> 8) & 0xFF) * kByteScale,
                                  static_cast<float>(color & 0xFF) * kByteScale};
            reinterpret_cast<SetDiffuse_t>(Offsets::FUN_M2MODEL_SET_DIFFUSE_COLOR)(model, rgb);
        } else if (kind == kProcAlpha) {
            const float alpha = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
            reinterpret_cast<SetAlpha_t>(Offsets::FUN_M2MODEL_SET_ALPHA)(model, alpha);
        }
    }
}

void Apply(void *pane, const uint8_t *kit, bool oneShot) {
    void *model = Pane::Model(pane);
    if (model == nullptr)
        return;
    void *scene = Pane::Scene(pane);
    for (int i = 0; i < static_cast<int>(sizeof(kAttachments) / sizeof(kAttachments[0])); ++i) {
        const uint8_t *effect = DBC::Record(
            Offsets::VAR_SPELLVISUALEFFECTNAME_RECORDS, Offsets::VAR_SPELLVISUALEFFECTNAME_COUNT,
            Game::Read<uint32_t>(kit, Offsets::OFF_SPELLVISUALKIT_EFFECTS + i * 4));
        if (effect == nullptr ||
            !reinterpret_cast<HasAttachment_t>(Offsets::FUN_M2MODEL_HAS_ATTACHMENT)(
                model, kAttachments[i]))
            continue;
        void *child = M2::CreateModel(
            scene, Game::Read<const char *>(effect, Offsets::OFF_SPELLVISUALEFFECTNAME_FILE));
        if (child == nullptr)
            continue;
        if (oneShot)
            M2::SetAnimDoneCallback(child, reinterpret_cast<const void *>(&OnEffectDone),
                                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pane)), 0);
        else
            M2::SetAnimDoneCallback(child, reinterpret_cast<const void *>(&OnEffectLoop), 0, 0);
        reinterpret_cast<Attach_t>(Offsets::FUN_M2MODEL_ATTACH)(child, model, kAttachments[i]);
        M2::Release(child);
    }
    Model::DisplayInfo::LightChildren(pane, model);
    ApplyProcedures(model, kit);
}

int __fastcall Script_ApplySpellVisualKit(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: ApplySpellVisualKit(spellVisualKitID [, oneShot])");
        return 0;
    }
    const uint8_t *kit =
        DBC::Record(Offsets::VAR_SPELLVISUALKIT_RECORDS, Offsets::VAR_SPELLVISUALKIT_COUNT,
                    static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)));
    if (kit != nullptr)
        Apply(pane, kit, Game::Lua::ToBoolean(L, 3) != 0);
    return 0;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"ApplySpellVisualKit", &Script_ApplySpellVisualKit},
};

const Game::Doc::Field kApplyArgs[] = {
    Game::Doc::Req("spellVisualKitID", "number", "A SpellVisualKit ID."),
    Game::Doc::Opt("oneShot", "boolean", "false",
                   "True plays the effects once; false keeps them until the model changes."),
};
const Game::Doc::Function kApply{"Shows a spell visual kit's effects on the model.", kApplyArgs,
                                 {}};
const Game::Doc::Method g_methodDocs[] = {
    {"ApplySpellVisualKit", &kApply},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Model::VisualKit
