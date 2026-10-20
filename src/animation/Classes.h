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

#pragma once

#include "animation/Object.h"

#include <cstdint>
#include <vector>

// 4.3.4's CSimpleAnimGroup and CSimpleAnim (and its Alpha, Translation,
// Scale and Rotation subclasses), ported from the decompiled client
// (wowdecomp15595 Engine/Source/Frame/CSimpleAnimGroup.cpp, CSimpleAnim.cpp).
// Field and function names follow it; the comments say where 1.12 differs.
namespace Animation {

class Anim;

enum LoopType { LOOP_NONE = 0, LOOP_REPEAT = 1, LOOP_BOUNCE = 2 };
enum LoopState { STATE_NONE = 0, STATE_FORWARD = 1, STATE_REVERSE = 2 };

// m_flags.
constexpr uint8_t GROUP_LOADING = 0x01;
constexpr uint8_t GROUP_PENDING_FINISH = 0x02;
constexpr uint8_t GROUP_PAUSED = 0x04;
constexpr uint8_t GROUP_STOPPING = 0x08;
constexpr uint8_t GROUP_IGNORE_THROTTLE = 0x10;

// Script indexes (Class::scriptNames order).
enum GroupScript { G_ONLOAD, G_ONPLAY, G_ONPAUSE, G_ONSTOP, G_ONFINISHED, G_ONUPDATE, G_ONLOOP };
enum AnimScript { A_ONLOAD, A_ONPLAY, A_ONPAUSE, A_ONSTOP, A_ONFINISHED, A_ONUPDATE };

class Group : public Object {
  public:
    static Class s_class;

    // `region`: the 1.12 region (frame, texture or font string) it animates.
    Group(void *region, const char *name);
    ~Group();

    void *RegionObject() const { return parent; }

    bool IsDone() const;
    bool IsPlaying() const { return loopState != STATE_NONE && !(flags & GROUP_PAUSED); }
    bool Play(bool reverse);
    void Pause();
    void Stop(bool requested);
    void Finish();
    bool OnUpdate(float elapsedSec);
    int GetMaxAnimOrder() const;
    bool HasAlphaAnim() const;
    bool HasPositionAnim() const;

    void NotifyAnimAdded(Anim *anim);
    void NotifyOrderChanged(Anim *anim, int oldOrder);
    void NotifyAnimRemoved(Anim *anim);
    void SortAnimations();
    void ComputeDuration();
    // XML: 4.3.4's PostLoadXML - every animation's, then sort, then OnLoad.
    void PostLoadXML();

    LoopType loopType = LOOP_NONE;
    float duration = 0.0f;
    float initialOffsetX = 0.0f, initialOffsetY = 0.0f; // layout units
    LoopState loopState = STATE_NONE;
    uint8_t flags = 0;
    int currentOrder = -1;
    float elapsed = 0.0f;
    float progress = 0.0f;
    std::vector<Anim *> animations;              // m_animations, creation order
    std::vector<std::vector<Anim *>> animLists;  // m_animLists, by order

  private:
    std::vector<Anim *> *FindAnimList(int order);
    std::vector<Anim *> *RegisterAnimList(int order);
    bool AddAnimListNode(std::vector<Anim *> *list, Anim *anim);
    bool RemoveAnimListNode(std::vector<Anim *> *list, Anim *anim);
    bool AdvanceLoopState(float elapsedSec, float remaining);
    bool DoGroupUpdateWork(float elapsedSec);
    void ResetAnims();
};

enum AnimKind { KIND_ANIMATION, KIND_ALPHA, KIND_TRANSLATION, KIND_SCALE, KIND_ROTATION };

class Anim : public Object {
  public:
    static Class s_class;            // "Animation"
    static Class s_alphaClass;       // "Alpha"
    static Class s_translationClass; // "Translation"
    static Class s_scaleClass;       // "Scale"
    static Class s_rotationClass;    // "Rotation"
    // The class of a CreateAnimation / XML type name; Animation for any
    // other (4.3.4's CreateAnimation falls back to it).
    static AnimKind KindOf(const char *typeName);

    Anim(Group *group, AnimKind kind, const char *name);
    ~Anim();

    Group *GetGroup() const { return static_cast<Group *>(parent); }
    void *GetRegionParent() const { return parent != nullptr ? GetGroup()->RegionObject() : nullptr; }

    bool Play();
    void Pause();
    void Stop(bool requested);
    bool IsDelaying() const;
    bool OnUpdate(float elapsedSec, float &consumed);
    void Apply(float progress);
    void SetStartDelay(float delay, bool notify);
    void SetEndDelay(float delay, bool notify);
    void SetDuration(float value, bool notify);
    void SetOrder(int value, bool notify, bool wrap);
    void SetSmoothing(float in, float out);
    bool GetSmoothing(float &in, float &out) const;
    bool ChangeParent(Group *group);
    // XML: 4.3.4's PostLoadXML - OnLoad.
    void PostLoadXML();

    AnimKind kind;
    float startDelay = 0.0f;
    float endDelay = 0.0f;
    float duration = 0.0f;
    float frameElapsed = 0.0f;
    float elapsed = 0.0f;
    float progressWithDelay = 0.0f;
    float progress = 0.0f;
    float smoothProgress = 0.0f;
    float orderPadding = 0.0f;
    int order = -1;
    uint8_t flags = 0;
    int playState = 0; // 0 stopped, 1 playing, 2 paused
    int loopState = STATE_NONE;
    void *target = nullptr;          // m_target, resolved from targetName on Play
    char targetName[128] = {};       // m_targetName
    bool hasSmoother = false;
    float smoothIn = 0.0f, smoothOut = 0.0f;

    // Alpha: m_change, -255..255.
    int16_t change = 0;
    // Translation: the offset; Scale: the scale and origin; Rotation: the
    // angle and origin. Offsets in layout units, as 4.3.4 keeps them.
    float offsetX = 0.0f, offsetY = 0.0f;
    float scaleChangeX = 0.0f, scaleChangeY = 0.0f; // m_scaleChange: 1 - scale
    float radians = 0.0f;
    int originPoint = 4; // FRAMEPOINT_CENTER
    float originX = 0.0f, originY = 0.0f;

  private:
    float UpdateProgress(float elapsedSec);
};

// 4.3.4's s_frameInterval: progress steps at most this often unless the
// group ignores the framerate throttle.
constexpr float kFrameInterval = 1.0f / 30.0f;

// AnimationGroup:CreateAnimation (Anim.cpp, where the animation classes are).
int __fastcall Script_CreateAnimation(void *L);

const char *LoopTypeToString(LoopType type);
bool StringToLoopType(const char *text, LoopType &type);
const char *LoopStateToString(int state);
bool StringToSmoothWeights(const char *text, float &in, float &out);
const char *SmoothWeightsToString(float in, float out);

} // namespace Animation
