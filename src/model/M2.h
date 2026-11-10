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

// Typed calls into the engine's M2 model instances and the Model frames
// ("panes") that show them, shared by every model/ module.

#pragma once

#include "Game.h"
#include "Offsets.h"

#include <cstddef>
#include <cstdint>

namespace Model::M2 {

inline bool IsLoaded(void *model, int tryLoad, int recurseChildren) {
    using Fn = int(__thiscall *)(void *, int, int);
    return reinterpret_cast<Fn>(Offsets::FUN_MODEL_INSTANCE_LOADED)(model, tryLoad,
                                                                    recurseChildren) != 0;
}

// The draw gate: also makes the device textures when `wait` is set.
inline bool IsReady(void *model, int wait, int recurseChildren) {
    using Fn = int(__thiscall *)(void *, int, int);
    return reinterpret_cast<Fn>(Offsets::FUN_MODEL_INSTANCE_READY)(model, wait,
                                                                   recurseChildren) != 0;
}

inline void AddRef(void *model) {
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_MODEL_INSTANCE_ADDREF)(model);
}

inline void Release(void *model) {
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_MODEL_INSTANCE_RELEASE)(model);
}

// A new model in `scene` that the caller owns one reference on, or null.
inline void *CreateModel(void *scene, const char *path) {
    using Fn = void *(__thiscall *)(void *, const char *, uint32_t);
    return reinterpret_cast<Fn>(Offsets::FUN_M2SCENE_CREATE_MODEL)(scene, path, 0);
}

// A new instance of `source`'s model in `scene` without its item attachments
// (helm, shoulders, held items), the way the portrait bake copies a unit:
// CreateModelFromModel (FUN_M2SCENE_COPY_MODEL), then DetachAllItemModels
// (FUN_DETACH_ALL_ITEM_MODELS). The caller owns one reference, or null.
inline void *CopyWithoutItems(void *scene, void *source) {
    using Copy_t = void *(__thiscall *)(void *, void *, uint32_t);
    using DetachAll_t = void(__fastcall *)(void *);
    void *copy = reinterpret_cast<Copy_t>(Offsets::FUN_M2SCENE_COPY_MODEL)(scene, source, 0);
    if (copy != nullptr)
        reinterpret_cast<DetachAll_t>(Offsets::FUN_DETACH_ALL_ITEM_MODELS)(copy);
    return copy;
}

inline void PlayAnimation(void *model, int keyBone, uint32_t id, int variation, int offset,
                          float speed, int blend, int primary) {
    using Fn = void(__thiscall *)(void *, int, uint32_t, int, int, float, int, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_PLAY_ANIMATION)(model, keyBone, id, variation,
                                                              offset, speed, blend, primary);
}

// The key bone's sequence as it plays (FUN_M2MODEL_GET_KEY_BONE_ANIM_STATE).
struct AnimState {
    uint32_t id;
    uint32_t variation;
    uint32_t position; // ms into the sequence
    float speed;
    uint32_t lo;
    uint32_t hi;
    uint32_t latch;
};
static_assert(sizeof(AnimState) == Offsets::M2_KEY_BONE_ANIM_STATE_SIZE);
static_assert(offsetof(AnimState, id) == Offsets::OFF_M2_KEY_BONE_ANIM_STATE_ID);
static_assert(offsetof(AnimState, variation) == Offsets::OFF_M2_KEY_BONE_ANIM_STATE_VARIATION);
static_assert(offsetof(AnimState, position) == Offsets::OFF_M2_KEY_BONE_ANIM_STATE_POSITION);
static_assert(offsetof(AnimState, speed) == Offsets::OFF_M2_KEY_BONE_ANIM_STATE_SPEED);
static_assert(offsetof(AnimState, lo) == Offsets::OFF_M2_KEY_BONE_ANIM_STATE_LO);
static_assert(offsetof(AnimState, hi) == Offsets::OFF_M2_KEY_BONE_ANIM_STATE_HI);
static_assert(offsetof(AnimState, latch) == Offsets::OFF_M2_KEY_BONE_ANIM_STATE_LATCH);

// Waits for an unloaded instance.
inline AnimState GetAnimState(void *model, int keyBone) {
    using Fn = void(__thiscall *)(void *, int, AnimState *);
    AnimState state;
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_KEY_BONE_ANIM_STATE)(model, keyBone, &state);
    return state;
}

// Where the key bone's animation stands, in ms into its sequence. Waits for
// an unloaded instance.
inline int GetAnimPosition(void *model, int keyBone) {
    return static_cast<int>(GetAnimState(model, keyBone).position);
}

// Moves the key bone's animation to `ms` into its sequence, keeping its speed.
inline void SetAnimPosition(void *model, int keyBone, int ms) {
    using Fn = void(__thiscall *)(void *, int, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_KEY_BONE_ANIM_TIME)(model, keyBone, ms);
}

inline void SetAnimDoneCallback(void *model, const void *callback, uint32_t userLow,
                                uint32_t userHigh) {
    using Fn = void(__thiscall *)(void *, const void *, uint32_t, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_ANIM_DONE_CALLBACK)(model, callback, userLow,
                                                                      userHigh);
}

inline void SetVisible(void *model, bool visible) {
    using Fn = void(__thiscall *)(void *, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_VISIBLE)(model, visible ? 1 : 0);
}

inline void SetAlpha(void *model, float alpha) {
    using Fn = void(__thiscall *)(void *, float);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_ALPHA)(model, alpha);
}

// The tint the model is drawn with, {r, g, b}; white is none.
inline void SetDiffuse(void *model, const float (&rgb)[3]) {
    using Fn = void(__thiscall *)(void *, const float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_DIFFUSE_COLOR)(model, rgb);
}

// CM2Model::FindChild: the model hung at attachment `attachId`, or null.
inline void *FindChild(void *model, int attachId) {
    using Fn = void *(__thiscall *)(void *, int);
    return reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_FIND_CHILD)(model, attachId);
}

// The model this one is attached to, or null.
inline void *Parent(const void *model) {
    return Game::Read<void *>(model, Offsets::OFF_M2MODEL_PARENT);
}

// CM2Model::Attach: `child` on the parent's attachment `id`. The child leaves
// the animate list (the parent animates it) and the parent takes a reference
// on it.
inline void Attach(void *child, void *parent, uint32_t id) {
    using Fn = void(__thiscall *)(void *, void *, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_ATTACH)(child, parent, id);
}

// Whether `child` may hang on `parent`: two models, and the parent is not the
// child or hung (at any depth) from it. Animate recurses through the parent
// chain (EnsureAnimated, 0x00714000), so a cycle never ends.
inline bool CanAttach(const void *child, const void *parent) {
    if (child == nullptr || parent == nullptr)
        return false;
    for (const void *m = parent; m != nullptr; m = Parent(m))
        if (m == child)
            return false;
    return true;
}

// CM2Model::HasAttachment. Waits for an unloaded model file.
inline bool HasAttachment(void *model, uint32_t id) {
    using Fn = int(__thiscall *)(void *, uint32_t);
    return reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_HAS_ATTACHMENT)(model, id) != 0;
}

// CM2Model::GetAttachmentPosition: the attachment's point in the space the
// root model's base matrix places it in (a scene's actor coordinates). The
// model must have the attachment (HasAttachment).
inline void GetAttachmentPosition(void *model, uint32_t id, float (&out)[3]) {
    using Fn = float *(__thiscall *)(void *, float *, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_ATTACHMENT_POSITION)(model, out, id);
}

// The animation-event callback (Offsets::FUN_M2MODEL_SET_EVENT_CALLBACK).
inline void SetEventCallback(void *model, const void *callback, uint32_t userLow,
                             uint32_t userHigh) {
    using Fn = void(__thiscall *)(void *, const void *, uint32_t, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_EVENT_CALLBACK)(model, callback, userLow,
                                                                  userHigh);
}

// CM2Model::HasEvent. Waits for an unloaded model file.
inline bool HasEvent(void *model, uint32_t tag) {
    using Fn = int(__thiscall *)(void *, uint32_t);
    return reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_HAS_EVENT)(model, tag) != 0;
}

// CM2Model::GetEventPosition: the event's point in the scene, as
// GetAttachmentPosition's. Waits for an unloaded model file.
inline void GetEventPosition(void *model, uint32_t tag, float (&out)[3]) {
    using Fn = float *(__thiscall *)(void *, float *, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_EVENT_POSITION)(model, out, tag);
}

inline bool HasLiveParticles(void *model) {
    using Fn = int(__thiscall *)(void *);
    return reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_HAS_LIVE_PARTICLES)(model) != 0;
}

// CM2Model::Detach, when the model is attached: off its parent, which drops
// its reference on it. Detach itself drops that reference unconditionally,
// so the world's Dismount (0x00607CE0) tests the parent first, as this does.
inline void Detach(void *model) {
    using Fn = void(__fastcall *)(void *);
    if (Parent(model) != nullptr)
        reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_DETACH)(model);
}

// The model's overall bounds {min xyz, max xyz}. Waits for an unloaded file.
inline void GetBoundingBox(void *model, float (&box)[6]) {
    using Fn = void(__thiscall *)(void *, float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_BOUNDING_BOX)(model, box);
}

// The model's bounding sphere {centre xyz, radius}. Waits for an unloaded file.
inline void GetBoundingSphere(void *model, float (&sphere)[4]) {
    using Fn = void(__thiscall *)(void *, float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_BOUNDING_SPHERE)(model, sphere);
}

// An animation's sequence record (FUN_M2MODEL_GET_SEQUENCE_INFO), all zero
// when the model has no sequence for it.
struct SequenceInfo {
    uint32_t flags;
    uint32_t length; // ms
    float speed;     // the move speed times the model's scale
    float bounds[6]; // {min xyz, max xyz}
    uint8_t rest[Offsets::M2_SEQUENCE_INFO_SIZE - 0x24];
};
static_assert(sizeof(SequenceInfo) == Offsets::M2_SEQUENCE_INFO_SIZE);
static_assert(offsetof(SequenceInfo, length) == Offsets::OFF_M2_SEQUENCE_INFO_LENGTH);
static_assert(offsetof(SequenceInfo, speed) == Offsets::OFF_M2_SEQUENCE_INFO_SPEED);
static_assert(offsetof(SequenceInfo, bounds) == Offsets::OFF_M2_SEQUENCE_INFO_BOUNDS);

// Waits for an unloaded file.
inline SequenceInfo GetSequenceInfo(void *model, uint32_t animation, uint32_t variation) {
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, SequenceInfo *);
    SequenceInfo info{};
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_SEQUENCE_INFO)(model, animation, variation, &info);
    return info;
}

// The bounds of the animation's sequence, all zero when the model has none.
// Waits for an unloaded file.
inline void GetSequenceBounds(void *model, uint32_t animation, uint32_t variation,
                              float (&box)[6]) {
    const SequenceInfo info = GetSequenceInfo(model, animation, variation);
    for (int i = 0; i < 6; ++i)
        box[i] = info.bounds[i];
}

// The world pick's steps (Offsets::FUN_M2SCENE_RAY_HIT): ray hits on, the
// candidates on the scene's hit list, then the ray.
inline void EnableRayHits(void *scene) {
    using Fn = void(__fastcall *)(void *);
    reinterpret_cast<Fn>(Offsets::FUN_M2SCENE_ENABLE_RAY_HITS)(scene);
}

// Puts the model on its scene's hit list (skinned triangles), with `value`
// for RayHit to return.
inline void SetHitTest(void *model, uintptr_t value) {
    using Fn = void(__thiscall *)(void *, int, int, uintptr_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_HIT_TEST_MODE)(model, Offsets::M2_HIT_TEST_SKINNED,
                                                                 0, value);
}

// The hit value of the nearest triangle on p0..p1 (view space), else 0.
// Empties the hit list.
inline uintptr_t RayHit(void *scene, const float (&p0)[3], const float (&p1)[3]) {
    using Fn = uintptr_t(__thiscall *)(void *, const float *, const float *, float *, int);
    float fraction = 1.0f;
    return reinterpret_cast<Fn>(Offsets::FUN_M2SCENE_RAY_HIT)(scene, p0, p1, &fraction, 1);
}

// The base matrix (row vectors: rows 0..2 the scaled axes, row 3 the
// translation).
inline void GetBaseTransform(void *model, float (&m)[16]) {
    using Fn = void(__thiscall *)(void *, float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_BASE_TRANSFORM)(model, m);
}

// CM2Model::SetBaseTransform: the base matrix as given.
inline void SetBaseTransform(void *model, const float (&m)[16]) {
    using Fn = void(__thiscall *)(void *, const float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_BASE_TRANSFORM)(model, m);
}

// CM2Model::SetBaseTransformTRS: identity, translated to `position`, rotated
// by `angle` about `axis`, then each row scaled.
inline void SetBaseTRS(void *model, const float (&position)[3], float angle, const float (&axis)[3],
                       const float (&scale)[3]) {
    using Fn = void(__thiscall *)(void *, const float *, float, const float *, const float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_BASE_TRS)(model, position, angle, axis, scale);
}

// The base matrix in place, for the engine's matrix calls after SetBaseTRS.
inline float *BaseMatrix(void *model) { return Game::Ptr<float>(model, Offsets::OFF_M2MODEL_BASE_MATRIX); }

inline float GetAlpha(void *model) {
    using Fn = float(__thiscall *)(void *);
    return reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_GET_ALPHA)(model);
}

inline void *FirstChild(const void *model) {
    return Game::Read<void *>(model, Offsets::OFF_MODEL_INSTANCE_CHILD_HEAD);
}
inline void *NextSibling(const void *model) {
    return Game::Read<void *>(model, Offsets::OFF_MODEL_INSTANCE_CHILD_NEXT);
}

inline void SetAnimated(void *model, bool animated) {
    using Fn = void(__thiscall *)(void *, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_ANIMATED)(model, animated ? 1 : 0);
}

inline void AnimateScene(void *scene, const float *cameraPos) {
    using Fn = void(__thiscall *)(void *, const float *);
    reinterpret_cast<Fn>(Offsets::FUN_M2SCENE_ANIMATE)(scene, cameraPos);
}

// M2SceneDraw: pass 0 the opaque models, 1 the translucent ones.
inline void DrawScene(void *scene, uint32_t pass) {
    using Fn = void(__thiscall *)(void *, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_M2SCENE_DRAW)(scene, pass);
}

inline void AttachItemVisuals(void *model, uint32_t visualID) {
    using Fn = void(__fastcall *)(void *, uint32_t);
    reinterpret_cast<Fn>(Offsets::FUN_ATTACH_ITEM_VISUALS)(model, visualID);
}

// CM2Model::SetDrawCallback: a callback the model's draw runs as one of its
// elements, in list 0 when `listZero`; a null callback removes it.
inline void SetDrawCallback(void *model, const void *callback, void *userData, bool listZero) {
    using Fn = void(__thiscall *)(void *, const void *, void *, int);
    reinterpret_cast<Fn>(Offsets::FUN_M2MODEL_SET_DRAW_CALLBACK)(model, callback, userData, listZero ? 1 : 0);
}

inline void SetRenderCallback(void *model, const void *callback, void *userData) {
    using Fn = void(__thiscall *)(void *, const void *, void *);
    reinterpret_cast<Fn>(Offsets::FUN_MODEL_INSTANCE_SET_RENDER_CB)(model, callback, userData);
}

// The model's shared M2 data (the file's header and tables).
inline const uint8_t *Data(void *model) {
    return Game::Read<const uint8_t *>(Game::Read<const uint8_t *>(model, Offsets::OFF_M2MODEL_SHARED),
                                       Offsets::OFF_M2SHARED_DATA);
}

// The index `id` maps to in one of the data's lookup tables
// (OFF_M2DATA_*_LOOKUP); the caller bounds it by that table's count.
inline uint16_t LookupIndex(void *model, uintptr_t tableOffset, uint32_t id) {
    using Fn = uint16_t(__fastcall *)(uint32_t, const void *);
    return reinterpret_cast<Fn>(Offsets::FUN_M2_LOOKUP_INDEX)(id, Data(model) + tableOffset);
}

// ---- a model light (M2LIGHT_SIZE bytes) -----------------------------------------

inline void InitLight(void *light) {
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_M2LIGHT_INITIALIZE)(light);
}
// 0 directional, 1 point.
inline void SetLightType(void *light, int type) {
    reinterpret_cast<void(__thiscall *)(void *, int)>(Offsets::FUN_M2LIGHT_SET_TYPE)(light, type);
}
inline void SetLightEnabled(void *light, bool enabled) {
    reinterpret_cast<void(__thiscall *)(void *, int)>(Offsets::FUN_M2LIGHT_SET_ENABLED)(light, enabled ? 1 : 0);
}
inline void SetLightDirection(void *light, const float *v) {
    reinterpret_cast<void(__thiscall *)(void *, const float *)>(Offsets::FUN_M2LIGHT_SET_DIRECTION)(light, v);
}
inline void SetLightPosition(void *light, const float *v) {
    reinterpret_cast<void(__thiscall *)(void *, const float *)>(Offsets::FUN_M2LIGHT_SET_POSITION)(light, v);
}

// ---- animation classes (Offsets::FUN_IS_*_ANIMATION) -----------------------------
// The unit's tests of an AnimationData ID. The death test is __stdcall, the
// others __fastcall.

inline bool IsDeathAnimation(int anim) {
    return reinterpret_cast<int(__stdcall *)(int)>(Offsets::FUN_IS_DEATH_ANIMATION)(anim) != 0;
}

namespace Detail {
inline bool AnimTest(uintptr_t fn, int anim) { return reinterpret_cast<int(__fastcall *)(int)>(fn)(anim) != 0; }
} // namespace Detail

inline bool IsBowAnimation(int anim) { return Detail::AnimTest(Offsets::FUN_IS_BOW_ANIMATION, anim); }
inline bool IsRifleAnimation(int anim) { return Detail::AnimTest(Offsets::FUN_IS_RIFLE_ANIMATION, anim); }
inline bool IsThrownAnimation(int anim) { return Detail::AnimTest(Offsets::FUN_IS_THROWN_ANIMATION, anim); }
inline bool IsEmoteAnimation(int anim) { return Detail::AnimTest(Offsets::FUN_IS_EMOTE_ANIMATION, anim); }
inline bool IsSpellCastAnimation(int anim) { return Detail::AnimTest(Offsets::FUN_IS_SPELL_CAST_ANIMATION, anim); }
inline bool IsMoveAnimation(int anim) { return Detail::AnimTest(Offsets::FUN_IS_MOVE_ANIMATION, anim); }

} // namespace Model::M2

namespace Model::Pane {

// The model instance a Model frame shows, or null.
inline void *Model(const void *pane) {
    return Game::Read<void *>(pane, Offsets::OFF_SIMPLEMODELFFX_MODEL_INSTANCE);
}

inline void *Scene(void *pane) {
    using Fn = void *(__thiscall *)(void *);
    return reinterpret_cast<Fn>(Offsets::FUN_SIMPLEMODEL_GET_SCENE)(pane);
}

// The frame's SetModelInstance virtual; null drops its model.
inline void SetModel(void *pane, void *model) {
    using Fn = void(__thiscall *)(void *, void *);
    Game::Read<Fn>(Game::Read<const void *>(pane, 0),
                   Offsets::VTBL_SIMPLEMODEL_SET_MODEL_INSTANCE)(pane, model);
}

// PlayerModel's SetDisplay: stores the display record the pane builds from.
inline void SetDisplay(void *pane, const uint8_t *record) {
    using Fn = void(__thiscall *)(void *, const uint8_t *);
    reinterpret_cast<Fn>(Offsets::FUN_PLAYERMODEL_SET_DISPLAY)(pane, record);
}

inline bool IsVisible(const void *pane) {
    return Game::Read<int>(pane, Offsets::OFF_FRAME_VISIBLE) != 0;
}

// The display record the pane shows (SetDisplay's), or null.
inline const uint8_t *DisplayRecordOf(const void *pane) {
    return Game::Read<const uint8_t *>(pane, Offsets::OFF_PLAYERMODEL_DISPLAY_RECORD);
}

// A display record of our own for SetDisplay. The pane reads only its
// display-ID field, keeps the pointer and builds from it on every show, so
// the record lives as long as the pane.
struct DisplayRecord {
    uint8_t bytes[Offsets::OFF_CREATURE_DISPLAYID + sizeof(uint32_t)] = {};
};

} // namespace Model::Pane
