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

// 4.3.4's CSimpleAnimGroup (wowdecomp15595 CSimpleAnimGroup.cpp and
// CSimpleAnimGroup_Scripts.cpp), on Animation::Object, with later clients'
// SetToFinalAlpha / IsSetToFinalAlpha: the final alpha is kept when the
// group finishes (Finish included), and Stop still restores the alpha. No
// source on disk says what a later client's Stop does with the flag.

#include "animation/Args.h"
#include "animation/Classes.h"
#include "animation/Region.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/DebugName.h"
#include "frame/ParentKey.h"

#include <algorithm>
#include <cmath>

namespace Animation {

namespace {

const char *const kGroupScripts[] = {"onload", "onplay", "onpause", "onstop", "onfinished", "onupdate", "onloop"};

} // namespace

// ---- Construction ---------------------------------------------------------------

Group::Group(void *region, const char *name) : Object(s_class, region, name) {
    if (region != nullptr)
        Region::RegisterGroup(region, this);
}

Group::~Group() {
    for (Anim *anim : animations)
        delete anim;
}

// ---- State --------------------------------------------------------------------

bool Group::IsDone() const {
    if (duration <= 0.0001f)
        return true;
    if (loopType == LOOP_REPEAT || loopType == LOOP_BOUNCE)
        return false;
    if (loopState == STATE_FORWARD || loopState == STATE_REVERSE)
        return false;
    return progress >= 1.0f;
}

void Group::Finish() {
    if (!IsDone() && loopState != STATE_NONE)
        flags |= GROUP_PENDING_FINISH;
}

bool Group::HasAlphaAnim() const {
    for (const Anim *anim : animations)
        if (anim->kind == KIND_ALPHA)
            return true;
    return false;
}

// The kinds that move a region's drawn corners or its string's position.
bool Group::HasPositionAnim() const {
    for (const Anim *anim : animations)
        switch (anim->kind) {
        case KIND_TRANSLATION:
        case KIND_SCALE:
        case KIND_ROTATION:
        case KIND_PATH:
        case KIND_LINE_TRANSLATION:
        case KIND_LINE_SCALE:
            return true;
        default:
            break;
        }
    return false;
}

// TextureCoordTranslation: the texture coordinates are stored and put back.
bool Group::HasTexCoordAnim() const {
    for (const Anim *anim : animations)
        if (anim->kind == KIND_TEXCOORD)
            return true;
    return false;
}

// ---- Order lists ----------------------------------------------------------------

std::vector<Anim *> *Group::FindAnimList(int order) {
    if (order == -1)
        return nullptr;
    int low = 0;
    int high = static_cast<int>(animLists.size()) - 1;
    while (low <= high) {
        const int middle = static_cast<int>(static_cast<unsigned>(high + low) >> 1);
        std::vector<Anim *> &list = animLists[middle];
        if (!list.empty()) {
            const int listOrder = list.front()->order;
            if (listOrder < order)
                low = middle + 1;
            else if (listOrder > order)
                high = middle - 1;
            else
                return &list;
        }
    }
    return nullptr;
}

std::vector<Anim *> *Group::RegisterAnimList(int order) {
    if (order == -1)
        return nullptr;
    const size_t count = animLists.size();
    if (count == 0) {
        animLists.emplace_back();
        return &animLists.back();
    }
    size_t index = count;
    for (size_t i = 0; i < count; ++i) {
        std::vector<Anim *> &list = animLists[i];
        if (list.empty())
            return &list;
        const int listOrder = list.front()->order;
        if (order == listOrder)
            return &list;
        if (order < listOrder) {
            index = i;
            break;
        }
    }
    animLists.insert(animLists.begin() + static_cast<ptrdiff_t>(index), std::vector<Anim *>());
    return &animLists[index];
}

bool Group::AddAnimListNode(std::vector<Anim *> *list, Anim *anim) {
    if (list == nullptr)
        return false;
    list->push_back(anim);
    return true;
}

bool Group::RemoveAnimListNode(std::vector<Anim *> *list, Anim *anim) {
    if (list == nullptr)
        return false;
    const auto it = std::find(list->begin(), list->end(), anim);
    if (it == list->end())
        return false;
    list->erase(it);
    if (list->empty())
        SortAnimations();
    return true;
}

void Group::ComputeDuration() {
    duration = 0.0f;
    for (std::vector<Anim *> &list : animLists) {
        float span = 0.0f;
        for (const Anim *anim : list) {
            const float total = anim->duration + anim->endDelay + anim->startDelay;
            if (total > span)
                span = total;
        }
        for (Anim *anim : list)
            anim->orderPadding = span - (anim->duration + anim->endDelay + anim->startDelay);
        duration += span;
    }
}

void Group::NotifyAnimAdded(Anim *anim) {
    if (anim == nullptr)
        return;
    // m_animations.LinkToTail: an animation already in the list moves to its tail.
    animations.erase(std::remove(animations.begin(), animations.end(), anim), animations.end());
    animations.push_back(anim);
    if (AddAnimListNode(RegisterAnimList(anim->order), anim))
        ComputeDuration();
}

void Group::NotifyOrderChanged(Anim *anim, int oldOrder) {
    if (anim == nullptr)
        return;
    if (RemoveAnimListNode(FindAnimList(oldOrder), anim))
        if (AddAnimListNode(RegisterAnimList(anim->order), anim))
            ComputeDuration();
}

void Group::NotifyAnimRemoved(Anim *anim) {
    if (anim == nullptr)
        return;
    animations.erase(std::remove(animations.begin(), animations.end(), anim), animations.end());
    std::vector<Anim *> *list = FindAnimList(anim->order);
    if (RemoveAnimListNode(list, anim)) {
        // RemoveAnimListNode sorts when the list emptied; the list may be gone.
        ComputeDuration();
    }
}

void Group::SortAnimations() {
    if (loopState == STATE_NONE || (flags & GROUP_PAUSED)) {
        loopState = STATE_NONE;
        flags &= 0xF0;
        currentOrder = -1;
        elapsed = 0.0f;
        progress = 0.0f;
    } else {
        Stop(false);
    }
    if (animations.empty()) {
        animLists.clear();
        return;
    }
    for (std::vector<Anim *> &list : animLists)
        list.clear();
    for (Anim *anim : animations)
        AddAnimListNode(RegisterAnimList(anim->order), anim);
    while (!animLists.empty() && animLists.back().empty())
        animLists.pop_back();
}

void Group::ResetAnims() {
    for (Anim *anim : animations) {
        anim->loopState = loopState > 0 && loopState <= 2 ? loopState : 0;
        anim->playState = 0;
        anim->elapsed = 0.0f;
        anim->progressWithDelay = 0.0f;
        anim->progress = 0.0f;
    }
}

// ---- Playback --------------------------------------------------------------------

void Group::Pause() {
    if (loopState == STATE_NONE || (flags & GROUP_PAUSED))
        return;
    flags |= GROUP_PAUSED;
    const std::vector<Anim *> list = animLists[currentOrder];
    for (Anim *anim : list)
        anim->Pause();
    RunScript(G_ONPAUSE, 0);
}

void Group::Stop(bool requested) {
    if (loopState == STATE_NONE || (flags & GROUP_STOPPING))
        return;
    flags |= GROUP_STOPPING;
    const std::vector<Anim *> list = animLists[currentOrder];
    for (Anim *anim : list)
        anim->Stop(requested);
    Region::NotifyEnd(RegionObject(), this);
    flags &= 0xF0;
    loopState = STATE_NONE;
    currentOrder = -1;
    elapsed = 0.0f;
    progress = 0.0f;
    if (HasScript(G_ONSTOP)) {
        Game::Lua::PushBool(Game::Lua::State(), requested);
        RunScript(G_ONSTOP, 1);
    }
}

bool Group::AdvanceLoopState(float /*elapsedSec*/, float /*remaining*/) {
    const int count = static_cast<int>(animLists.size());
    const bool reverse = loopState == STATE_REVERSE;
    const int step = reverse ? -1 : 1;
    const int first = reverse ? count - 1 : 0;
    const int end = reverse ? -1 : count;

    currentOrder += step;
    if (currentOrder == end) {
        if (loopType == LOOP_NONE || (flags & GROUP_PENDING_FINISH))
            return true;
        if (loopType == LOOP_BOUNCE) {
            loopState = static_cast<LoopState>(!reverse + 1);
            startedReverse = !startedReverse; // 3.80.2's BOUNCE flips +0x81
            currentOrder -= step;
        } else {
            currentOrder = first;
        }
        elapsed = 0.0f;
        progress = 0.0f;
        ResetAnims();
        if (HasScript(G_ONLOOP)) {
            Game::Lua::PushString(Game::Lua::State(),
                                  LoopStateToString(startedReverse ? STATE_REVERSE : STATE_FORWARD));
            RunScript(G_ONLOOP, 1);
        }
        if (loopState == STATE_NONE || (flags & GROUP_PAUSED))
            return true;
    }
    const std::vector<Anim *> list = animLists[currentOrder];
    for (Anim *anim : list)
        anim->Play();
    return false;
}

// 3.80.2's update (FUN_1405A8220 / FUN_1405B15A0): the frame's step is
// clamped to 0..60 s, each animation advances by it times the speed
// multiplier, and the group's elapsed time adds the largest scaled step.
// OnUpdate scripts get the frame's step. The step's real time is the scaled
// step over the multiplier; a multiplier that is not positive moves the
// animations once and ends the update.
bool Group::DoGroupUpdateWork(float elapsedSec) {
    float remaining = elapsedSec < 0.0f ? 0.0f : (elapsedSec >= 60.0f ? 60.0f : elapsedSec);
    while (true) {
        for (Anim *anim : animLists[currentOrder]) {
            void *region = anim->GetRegionParent();
            if (region != nullptr)
                Region::PreOnAnimUpdate(region);
        }
        for (int i = 0; i < currentOrder; ++i)
            for (Anim *anim : animLists[i])
                anim->Apply(1.0f);

        float maxConsumed = 0.0f;
        bool finished = true;
        const std::vector<Anim *> list = animLists[currentOrder];
        for (Anim *anim : list) {
            float consumed;
            finished = anim->OnUpdate(remaining * speedMultiplier, remaining, consumed) && finished;
            if (consumed > maxConsumed)
                maxConsumed = consumed;
        }

        if (!(flags & GROUP_PAUSED) && loopState != STATE_NONE) {
            remaining = speedMultiplier > 0.0f ? remaining - maxConsumed / speedMultiplier : 0.0f;
            elapsed += maxConsumed;
            progress = elapsed < duration ? elapsed / duration : 1.0f;
            if (finished && AdvanceLoopState(elapsedSec, remaining))
                return true;
        }

        if (remaining <= 0.0001f || IsDone() || loopState == STATE_NONE || (flags & GROUP_PAUSED))
            return false;
    }
}

bool Group::OnUpdate(float elapsedSec) {
    if (IsDone() || loopState == STATE_NONE)
        return true;
    const bool done = DoGroupUpdateWork(elapsedSec);
    if (HasScript(G_ONUPDATE)) {
        Game::Lua::PushNumber(Game::Lua::State(), elapsedSec);
        RunScript(G_ONUPDATE, 1);
    }
    if (done) {
        // setToFinalAlpha: the alpha each alpha animation's target finished on
        // becomes what the end restores. Stop restores the alpha as before.
        if (setToFinalAlpha)
            for (Anim *anim : animations)
                if (anim->kind == KIND_ALPHA)
                    Region::KeepAnimAlpha(RegionObject(), anim->target);
        Region::NotifyEnd(RegionObject(), this);
        const bool requested = (flags & GROUP_PENDING_FINISH) != 0;
        loopState = STATE_NONE;
        flags &= 0xF0;
        currentOrder = -1;
        elapsed = 0.0f;
        progress = 0.0f;
        if (HasScript(G_ONFINISHED)) {
            Game::Lua::PushBool(Game::Lua::State(), requested);
            RunScript(G_ONFINISHED, 1);
        }
    }
    return IsDone();
}

// 3.80.2's Play: a paused group resumes where it is (`reverse` ignored); a
// playing one already going that way, or looping, is left alone; a playing
// one going the other way turns around in place, with no OnPlay.
bool Group::Play(bool reverse, float offset) {
    if (animations.empty())
        return false;
    if (flags & GROUP_PAUSED) {
        flags &= ~GROUP_PAUSED;
    } else if (loopState != STATE_NONE) {
        if (reverse != (loopState == STATE_REVERSE) && loopType == LOOP_NONE)
            FlipDirection();
        return true;
    } else {
        flags &= 0xF0;
        loopState = STATE_NONE;
        currentOrder = reverse ? 0x7FFF : -1;
        elapsed = 0.0f;
        progress = 0.0f;
        startedReverse = reverse;
    }
    RunScript(G_ONPLAY, 0);
    const int count = static_cast<int>(animLists.size());
    if (count > 0) {
        if (currentOrder < 0 || currentOrder >= count) {
            if (currentOrder < 0) {
                loopState = STATE_FORWARD;
                currentOrder = 0;
            } else {
                loopState = STATE_REVERSE;
                currentOrder = count - 1;
            }
            ResetAnims();
        }
        const std::vector<Anim *> list = animLists[currentOrder];
        for (Anim *anim : list)
            anim->Play();
        Region::NotifyBegin(RegionObject(), this);
    }
    if (loopState != STATE_NONE && !(flags & GROUP_PAUSED))
        OnUpdate(offset);
    return true;
}

// Every animation turns: the current order's mirror where they are, the
// others start over in the new direction when their order comes. The orders
// before the current one stay at their end, as the update draws them.
void Group::FlipDirection() {
    const LoopState next = loopState == STATE_REVERSE ? STATE_FORWARD : STATE_REVERSE;
    loopState = next;
    elapsed = duration - elapsed;
    if (elapsed < 0.0f)
        elapsed = 0.0f;
    progress = duration > 0.0f && elapsed < duration ? elapsed / duration : 1.0f;
    for (int i = 0; i < static_cast<int>(animLists.size()); ++i) {
        for (Anim *anim : animLists[i]) {
            anim->loopState = next;
            if (i == currentOrder) {
                anim->Mirror();
            } else {
                anim->playState = 0;
                anim->elapsed = 0.0f;
                anim->progressWithDelay = 0.0f;
                anim->progress = 0.0f;
            }
        }
    }
}

void Group::RemoveAnimations() {
    Stop(false);
    const std::vector<Anim *> removed = std::move(animations);
    animations.clear();
    animLists.clear();
    ComputeDuration();
    for (Anim *anim : removed)
        Anim::Release(anim);
}

void Group::PostLoadXML() {
    flags &= ~GROUP_LOADING;
    const std::vector<Anim *> list = animations;
    for (Anim *anim : list)
        anim->PostLoadXML();
    SortAnimations();
    ComputeDuration();
    RunScript(G_ONLOAD, 0);
}

// ---- Strings ----------------------------------------------------------------------

namespace {

struct Named {
    int value;
    const char *name;
};
const Named kLoopTypes[] = {{LOOP_NONE, "NONE"}, {LOOP_REPEAT, "REPEAT"}, {LOOP_BOUNCE, "BOUNCE"}};
const Named kLoopStates[] = {{STATE_NONE, "NONE"}, {STATE_FORWARD, "FORWARD"}, {STATE_REVERSE, "REVERSE"}};

struct Weights {
    float in, out;
    const char *name;
};
const Weights kSmoothWeights[] = {
    {0.0f, 0.0f, "NONE"}, {1.0f, 0.0f, "IN"}, {0.0f, 1.0f, "OUT"}, {1.0f, 1.0f, "IN_OUT"}, {1.0f, 1.0f, "OUT_IN"},
};

} // namespace

const char *LoopTypeToString(LoopType type) {
    for (const Named &n : kLoopTypes)
        if (n.value == type)
            return n.name;
    return "UNKNOWN";
}

bool StringToLoopType(const char *text, LoopType &type) {
    for (const Named &n : kLoopTypes)
        if (text != nullptr && Ascii::EqualCI(n.name, text)) {
            type = static_cast<LoopType>(n.value);
            return true;
        }
    return false;
}

const char *LoopStateToString(int state) {
    for (const Named &n : kLoopStates)
        if (n.value == state)
            return n.name;
    return "UNKNOWN";
}

bool StringToSmoothWeights(const char *text, float &in, float &out) {
    for (const Weights &w : kSmoothWeights)
        if (text != nullptr && Ascii::EqualCI(w.name, text)) {
            in = w.in;
            out = w.out;
            return true;
        }
    return false;
}

const char *SmoothWeightsToString(float in, float out) {
    for (const Weights &w : kSmoothWeights)
        if (std::fabs(in - w.in) < 0.001f && std::fabs(out - w.out) < 0.001f)
            return w.name;
    return "UNKNOWN";
}

// ---- Lua methods (CSimpleAnimGroup_Scripts.cpp, 3.80.2's table 0x14458F9B0) -----
//
// Arguments are read as 3.80.2's generated bindings read them (Args.h).

namespace {

Group *Self(void *L) { return static_cast<Group *>(Resolve(L, 1, Group::s_class)); }

constexpr char kPlayUsage[] = "Usage: self:Play([reverse, offset])";
constexpr char kRestartUsage[] = "Usage: self:Restart([reverse, offset])";

// Play's and Restart's [reverse, offset]: false and 0 by default.
bool PlayArgs(void *L, const char *usage, bool &reverse, float &offset) {
    reverse = Args::OptBool(L, 2, false);
    offset = 0.0f;
    return Args::OptNumber(L, 3, usage, offset);
}

int __fastcall Script_Play(void *L) {
    Group *g = Self(L);
    bool reverse;
    float offset;
    if (g != nullptr && PlayArgs(L, kPlayUsage, reverse, offset))
        g->Play(reverse, offset);
    return 0;
}
int __fastcall Script_Restart(void *L) {
    Group *g = Self(L);
    bool reverse;
    float offset;
    if (g == nullptr || !PlayArgs(L, kRestartUsage, reverse, offset))
        return 0;
    g->Stop(true);
    g->Play(reverse, offset);
    return 0;
}
int __fastcall Script_SetPlaying(void *L) {
    Group *g = Self(L);
    bool play;
    if (g == nullptr || !Args::Bool(L, 2, "Usage: self:SetPlaying(play)", play))
        return 0;
    if (play)
        g->Play(false, 0.0f);
    else
        g->Stop(true);
    return 0;
}
int __fastcall Script_Pause(void *L) {
    if (Group *g = Self(L))
        g->Pause();
    return 0;
}
int __fastcall Script_Stop(void *L) {
    if (Group *g = Self(L))
        g->Stop(true);
    return 0;
}
int __fastcall Script_Finish(void *L) {
    if (Group *g = Self(L))
        g->Finish();
    return 0;
}
int __fastcall Script_RemoveAnimations(void *L) {
    if (Group *g = Self(L))
        g->RemoveAnimations();
    return 0;
}

template <typename Fn> int PushNumberOf(void *L, Fn fn) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushNumber(L, fn(g));
    return 1;
}
template <typename Fn> int PushBoolOf(void *L, Fn fn) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushBool(L, fn(g));
    return 1;
}

// No zero guard: a group with no duration gives NaN, as 3.80.2's.
int __fastcall Script_GetProgress(void *L) { return PushNumberOf(L, [](Group *g) { return g->elapsed / g->duration; }); }
int __fastcall Script_GetElapsed(void *L) { return PushNumberOf(L, [](Group *g) { return g->elapsed; }); }
int __fastcall Script_GetDuration(void *L) { return PushNumberOf(L, [](Group *g) { return g->duration; }); }
int __fastcall Script_GetAnimationSpeedMultiplier(void *L) {
    return PushNumberOf(L, [](Group *g) { return g->speedMultiplier; });
}
int __fastcall Script_IsDone(void *L) { return PushBoolOf(L, [](Group *g) { return g->IsDone(); }); }
int __fastcall Script_IsPlaying(void *L) { return PushBoolOf(L, [](Group *g) { return g->IsPlaying(); }); }
int __fastcall Script_IsPaused(void *L) {
    return PushBoolOf(L, [](Group *g) { return (g->flags & GROUP_PAUSED) != 0; });
}
int __fastcall Script_IsPendingFinish(void *L) {
    return PushBoolOf(L, [](Group *g) { return (g->flags & GROUP_PENDING_FINISH) != 0; });
}
int __fastcall Script_IsReverse(void *L) { return PushBoolOf(L, [](Group *g) { return g->StartedReverse(); }); }
int __fastcall Script_IsSetToFinalAlpha(void *L) { return PushBoolOf(L, [](Group *g) { return g->setToFinalAlpha; }); }

// No range check: 0 and negative multipliers are accepted, as 3.80.2's.
int __fastcall Script_SetAnimationSpeedMultiplier(void *L) {
    Group *g = Self(L);
    float value;
    if (g != nullptr &&
        Args::Number(L, 2, "Usage: self:SetAnimationSpeedMultiplier(animationSpeedMultiplier)", value))
        g->speedMultiplier = value;
    return 0;
}
int __fastcall Script_SetLooping(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    LoopType type;
    // A string; a number is not one here (3.80.2's FUN_140AB2EA0).
    if (Game::Lua::Type(L, 2) != Game::Lua::TYPE_STRING || !StringToLoopType(Game::Lua::ToString(L, 2), type)) {
        Args::Fail(L, 2, "Usage: self:SetLooping(loopType)");
        return 0;
    }
    g->loopType = type;
    return 0;
}
int __fastcall Script_GetLooping(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushString(L, LoopTypeToString(g->loopType));
    return 1;
}
int __fastcall Script_GetLoopState(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    const int state = g->loopState == STATE_NONE ? STATE_NONE : (g->startedReverse ? STATE_REVERSE : STATE_FORWARD);
    Game::Lua::PushString(L, LoopStateToString(state));
    return 1;
}
int __fastcall Script_SetToFinalAlpha(void *L) {
    Group *g = Self(L);
    bool keep;
    if (g != nullptr && Args::Bool(L, 2, "Usage: self:SetToFinalAlpha(setToFinalAlpha)", keep))
        g->setToFinalAlpha = keep;
    return 0;
}
// Set/GetIgnoreFramerateThrottle: 4.3.4's, kept for addons that call them.
int __fastcall Script_SetIgnoreFramerateThrottle(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    if (Game::Lua::ToFlag(L, 2))
        g->flags |= GROUP_IGNORE_THROTTLE;
    else
        g->flags &= ~GROUP_IGNORE_THROTTLE;
    return 0;
}
int __fastcall Script_GetIgnoreFramerateThrottle(void *L) {
    return PushBoolOf(L, [](Group *g) { return (g->flags & GROUP_IGNORE_THROTTLE) != 0; });
}
int __fastcall Script_GetAnimations(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    const int count = static_cast<int>(g->animations.size());
    if (!Game::Lua::CheckStack(L, count)) {
        Game::Lua::Error(L, "%s:GetAnimations(): Stack overflow", g->DisplayName());
        return 0;
    }
    for (Anim *anim : g->animations)
        Push(L, anim);
    return count;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"GetObjectType", &Script_GetObjectType},
    {"IsObjectType", &Script_IsObjectType},
    {"GetName", &Script_GetName},
    {"GetParent", &Script_GetParent},
    {"GetDebugName", &Frame::DebugName::Script_GetDebugName},
    {"GetParentKey", &Frame::ParentKey::Script_GetParentKey},
    {"SetParentKey", &Frame::ParentKey::Script_SetParentKey},
    {"ClearParentKey", &Frame::ParentKey::Script_ClearParentKey},
    {"CreateAnimation", &Script_CreateAnimation},
    {"Finish", &Script_Finish},
    {"GetAnimationSpeedMultiplier", &Script_GetAnimationSpeedMultiplier},
    {"GetAnimations", &Script_GetAnimations},
    {"GetDuration", &Script_GetDuration},
    {"GetElapsed", &Script_GetElapsed},
    {"GetIgnoreFramerateThrottle", &Script_GetIgnoreFramerateThrottle},
    {"GetLoopState", &Script_GetLoopState},
    {"GetLooping", &Script_GetLooping},
    {"GetProgress", &Script_GetProgress},
    {"GetScript", &Script_GetScript},
    {"HasScript", &Script_HasScript},
    {"HookScript", &Script_HookScript},
    {"IsDone", &Script_IsDone},
    {"IsPaused", &Script_IsPaused},
    {"IsPendingFinish", &Script_IsPendingFinish},
    {"IsPlaying", &Script_IsPlaying},
    {"IsReverse", &Script_IsReverse},
    {"IsSetToFinalAlpha", &Script_IsSetToFinalAlpha},
    {"Pause", &Script_Pause},
    {"Play", &Script_Play},
    {"RemoveAnimations", &Script_RemoveAnimations},
    {"Restart", &Script_Restart},
    {"SetAnimationSpeedMultiplier", &Script_SetAnimationSpeedMultiplier},
    {"SetIgnoreFramerateThrottle", &Script_SetIgnoreFramerateThrottle},
    {"SetLooping", &Script_SetLooping},
    {"SetPlaying", &Script_SetPlaying},
    {"SetScript", &Script_SetScript},
    {"SetToFinalAlpha", &Script_SetToFinalAlpha},
    {"Stop", &Script_Stop},
};

} // namespace

Class Group::s_class{"AnimationGroup", nullptr, kGroupScripts, 7, kMethods,
                     static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0])), 0, 0};

} // namespace Animation
