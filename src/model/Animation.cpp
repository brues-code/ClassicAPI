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

// `PlayerModel:SetAnimation(anim [, variation])`, `FreezeAnimation(anim,
// variation, frame)`, `SetDoBlend` / `GetDoBlend` and `HasAnimation(anim)` —
// retail's CharacterModelBase animation methods.
//
// A pane rests on Stand: its per-paint UpdateModel (FUN_PLAYERMODEL_UPDATE_MODEL)
// arms Stand whenever key bone -1 plays anything else and no turn is pending,
// so any other animation lasts one frame. 4.3.4's CGCharacterModelBase has
// the same UpdateModel with the pane's m_sequence / m_sequenceVariation in
// place of Stand, reset to Stand each time the pane sets up a new model
// (SetUpModel, the display setup, SetCustomRace), and a RotateModel that
// skips the shuffle while m_sequence is not Stand. Retail's SetAnimation sets
// that pair. 1.12.1 has no such fields, so:
//   - SetAnimation stores the pair per pane; UpdateModel is hooked to run
//     4.3.4's version for a pane that has one, and stock otherwise.
//   - SetModelInstance (FUN_SIMPLEMODEL_SET_MODEL_INSTANCE, which every pane
//     model build and drop goes through) clears it.
//   - Model::Rotation asks HasRestingAnimation before it shuffles.
// The re-arm also restarts the animation after the pane's done callback
// (0x005062A0) arms Stand at its end, so it loops as in 4.3.4. A pane is
// also re-armed once after each SetAnimation / FreezeAnimation, so a call
// with the animation that already plays still starts or ends a freeze.
//
// FreezeAnimation rests the pane on the animation held at one time. The
// engine's sequence time is start + (ftol(elapsed * speed) + offset) mod
// length, so the arm plays at speed 0 and writes `frame` (milliseconds, the
// unit of M2 sequence times) to the slot's offset. Speed 0 also puts the end
// stamp on the start stamp, which would queue the done callback at once and
// so arm Stand. The arm sets the slot's done latch, the flag Update sets
// after it reports done: a held animation never ends.
//
// HasAnimation is PlayAnimation's own test on the raw id: the lookup index
// must be below the sequence count.

#include "model/Animation.h"

#include "Game.h"
#include "Offsets.h"
#include "model/M2.h"
#include "time/Clock.h"

#include <cstdint>
#include <unordered_map>

namespace Model::Animation {
namespace {

using PaneFn_t = void(__thiscall *)(void *pane);
using SetModelInstance_t = void(__thiscall *)(void *frame, void *model);
using GetKeyBoneAnim_t = uint32_t(__thiscall *)(void *model, int keyBone);

struct PaneState {
    bool doBlend = false;
    bool resting = false; // SetAnimation gave the pane a resting animation
    bool pending = false; // set since the last arm
    bool frozen = false;  // FreezeAnimation: held at `frame`
    uint32_t animation = 0;
    int variation = 0;
    uint32_t frame = 0; // ms into the sequence
};
std::unordered_map<void *, PaneState> g_panes;

PaneFn_t g_origUpdateModel = nullptr;
SetModelInstance_t g_origSetModelInstance = nullptr;

uint32_t KeyBoneAnim(void *model) {
    return reinterpret_cast<GetKeyBoneAnim_t>(Offsets::FUN_M2MODEL_GET_KEY_BONE_ANIM)(model, -1);
}

void Arm(void *model, const PaneState &s) {
    M2::PlayAnimation(model, -1, s.animation, s.variation, 0, s.frozen ? 0.0f : 1.0f,
                      s.doBlend ? 1 : 0, 1);
    // PlayAnimation stores the id only when it found a sequence to play.
    if (!s.frozen || KeyBoneAnim(model) != s.animation)
        return;
    auto *bone = Game::Read<uint8_t *>(model, Offsets::OFF_M2MODEL_BONE_STATES);
    Game::Ref<uint32_t>(bone, Offsets::OFF_M2BONE_SEQUENCE_OFFSET) = s.frame;
    Game::Ref<int>(bone, Offsets::OFF_M2BONE_DONE_REPORTED) = 1;
}

void __fastcall UpdateModel_h(void *pane, void * /*edx*/) {
    const auto it = g_panes.find(pane);
    if (it == g_panes.end() || !it->second.resting) {
        g_origUpdateModel(pane);
        return;
    }
    PaneState &s = it->second;
    reinterpret_cast<PaneFn_t>(Offsets::FUN_SIMPLEMODEL_UPDATE_MODEL)(pane);
    void *model = Pane::Model(pane);
    const uint32_t deadline = Game::Read<uint32_t>(pane, Offsets::OFF_PLAYERMODEL_TURN_DEADLINE);
    if (Game::Read<int>(pane, Offsets::OFF_PLAYERMODEL_TURNING) == 0 &&
        (deadline == 0 || Time::Clock::Reached(deadline)) && model != nullptr &&
        M2::IsLoaded(model, 0, 0) && (s.pending || KeyBoneAnim(model) != s.animation)) {
        Arm(model, s);
        s.pending = false;
        Game::Ref<uint32_t>(pane, Offsets::OFF_PLAYERMODEL_TURN_DEADLINE) = 0;
    }
    Game::Ref<int>(pane, Offsets::OFF_PLAYERMODEL_TURNING) = 0;
}

void __fastcall SetModelInstance_h(void *frame, void * /*edx*/, void *model) {
    const auto it = g_panes.find(frame);
    if (it != g_panes.end())
        it->second.resting = false;
    g_origSetModelInstance(frame, model);
}

const Game::HookAutoRegister _updateHook{Offsets::FUN_PLAYERMODEL_UPDATE_MODEL,
                                         reinterpret_cast<void *>(&UpdateModel_h),
                                         reinterpret_cast<void **>(&g_origUpdateModel)};
const Game::HookAutoRegister _setModelHook{Offsets::FUN_SIMPLEMODEL_SET_MODEL_INSTANCE,
                                           reinterpret_cast<void *>(&SetModelInstance_h),
                                           reinterpret_cast<void **>(&g_origSetModelInstance)};

// SetAnimation / FreezeAnimation: the pane rests on `animation`, re-armed on
// the next update.
void Rest(void *pane, uint32_t animation, int variation, bool frozen, uint32_t frame) {
    PaneState &s = g_panes[pane];
    s.resting = true;
    s.pending = true;
    s.frozen = frozen;
    s.animation = animation;
    s.variation = variation;
    s.frame = frame;
}

int __fastcall Script_SetAnimation(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: SetAnimation(animation [, variation])");
        return 0;
    }
    Rest(pane, static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)),
         Game::Lua::IsNumber(L, 3) ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : 0, false, 0);
    return 0;
}

int __fastcall Script_FreezeAnimation(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L, "Usage: FreezeAnimation(animation, variation, frame)");
        return 0;
    }
    const double frame = Game::Lua::ToNumber(L, 4);
    Rest(pane, static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)),
         static_cast<int>(Game::Lua::ToNumber(L, 3)), true,
         frame > 0.0 ? static_cast<uint32_t>(frame) : 0);
    return 0;
}

int __fastcall Script_SetDoBlend(void *L) {
    if (void *pane = Game::Lua::ResolvePlayerModel(L))
        g_panes[pane].doBlend = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __fastcall Script_GetDoBlend(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    const auto it = g_panes.find(pane);
    Game::Lua::PushBool(L, it != g_panes.end() && it->second.doBlend);
    return 1;
}

int __fastcall Script_HasAnimation(void *L) {
    void *pane = Game::Lua::ResolvePlayerModel(L);
    if (pane == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: HasAnimation(animation)");
        return 0;
    }
    const auto id = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    bool has = false;
    void *model = Pane::Model(pane);
    if (model != nullptr && M2::IsLoaded(model, 0, 0))
        has = M2::LookupIndex(model, Offsets::OFF_M2DATA_ANIMATION_LOOKUP, id) <
              Game::Read<uint32_t>(M2::Data(model), Offsets::OFF_M2DATA_SEQUENCE_COUNT);
    Game::Lua::PushBool(L, has);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetAnimation", &Script_SetAnimation},
    {"FreezeAnimation", &Script_FreezeAnimation},
    {"SetDoBlend", &Script_SetDoBlend},
    {"GetDoBlend", &Script_GetDoBlend},
    {"HasAnimation", &Script_HasAnimation},
};

const Game::Doc::Field kSetAnimationArgs[] = {
    Game::Doc::Req("animation", "number", "An AnimationData ID."),
    Game::Doc::Opt("variation", "number", "0", "The variation to play; -1 picks one at random."),
};
const Game::Doc::Function kSetAnimation{
    "Sets the animation the model plays until it loads a new model.", kSetAnimationArgs, {}};
const Game::Doc::Field kFreezeArgs[] = {
    Game::Doc::Req("animation", "number", "An AnimationData ID."),
    Game::Doc::Req("variation", "number", "The variation; -1 picks one at random."),
    Game::Doc::Req("frame", "number", "The time into the animation, in milliseconds."),
};
const Game::Doc::Function kFreezeAnimation{
    "Holds the model in one pose of an animation until it loads a new model.", kFreezeArgs, {}};
const Game::Doc::Field kDoBlendArgs[] = {
    Game::Doc::Opt("doBlend", "bool", "false"),
};
const Game::Doc::Function kSetDoBlend{
    "Sets whether the model blends into the animation that SetAnimation set.", kDoBlendArgs,
    {}};
const Game::Doc::Field kDoBlendRets[] = {
    Game::Doc::Req("doBlend", "bool"),
};
const Game::Doc::Function kGetDoBlend{"Returns whether the model blends into its animation.",
                                      {}, kDoBlendRets};
const Game::Doc::Field kHasAnimationArgs[] = {
    Game::Doc::Req("animation", "number", "An AnimationData ID."),
};
const Game::Doc::Field kHasAnimationRets[] = {
    Game::Doc::Req("hasAnimation", "bool"),
};
const Game::Doc::Function kHasAnimation{
    "Returns whether the model has the animation. False until the model has loaded.",
    kHasAnimationArgs, kHasAnimationRets};
const Game::Doc::Method g_methodDocs[] = {
    {"SetAnimation", &kSetAnimation},
    {"FreezeAnimation", &kFreezeAnimation},
    {"SetDoBlend", &kSetDoBlend},
    {"GetDoBlend", &kGetDoBlend},
    {"HasAnimation", &kHasAnimation},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_PLAYERMODEL_METHOD_REGISTRY), g_methods,
        g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Frames are rebuilt on /reload and a new frame can reuse an old address.
void PrepareForReload() {
    g_panes.clear();
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace

bool HasRestingAnimation(void *pane) {
    const auto it = g_panes.find(pane);
    return it != g_panes.end() && it->second.resting && it->second.animation != 0;
}

} // namespace Model::Animation
