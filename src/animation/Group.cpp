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
// CSimpleAnimGroup_Scripts.cpp), on Animation::Object.

#include "animation/Classes.h"
#include "animation/Region.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"

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

int Group::GetMaxAnimOrder() const {
    if (animLists.empty())
        return -1;
    return animLists.back().front()->order;
}

bool Group::HasAlphaAnim() const {
    for (const Anim *anim : animations)
        if (anim->kind == KIND_ALPHA)
            return true;
    return false;
}

bool Group::HasPositionAnim() const {
    for (const Anim *anim : animations)
        if (anim->kind != KIND_ALPHA)
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
    int nextOrder = 0;
    for (Anim *anim : animations) {
        int order = anim->order;
        if (order == -1) {
            order = nextOrder++;
            anim->order = order < 0 ? 0 : order;
        } else {
            nextOrder = order + 1;
        }
        if (nextOrder > 99)
            nextOrder = 0;
        AddAnimListNode(RegisterAnimList(order), anim);
    }
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
            currentOrder -= step;
        } else {
            currentOrder = first;
        }
        elapsed = 0.0f;
        progress = 0.0f;
        ResetAnims();
        if (HasScript(G_ONLOOP)) {
            Game::Lua::PushString(Game::Lua::State(), LoopStateToString(loopState));
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
            finished = anim->OnUpdate(remaining, consumed) && finished;
            if (consumed > maxConsumed)
                maxConsumed = consumed;
        }

        if (!(flags & GROUP_PAUSED) && loopState != STATE_NONE) {
            remaining -= maxConsumed;
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

bool Group::Play(bool reverse) {
    if (animations.empty())
        return false;
    if (loopState != STATE_NONE && !(flags & GROUP_PAUSED))
        return true;
    if (flags & GROUP_PAUSED) {
        flags &= ~GROUP_PAUSED;
    } else {
        flags &= 0xF0;
        loopState = STATE_NONE;
        currentOrder = reverse ? 0x7FFF : -1;
        elapsed = 0.0f;
        progress = 0.0f;
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
    OnUpdate(0.0f);
    return true;
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

// ---- Lua methods (CSimpleAnimGroup_Scripts.cpp) -------------------------------

namespace {

Group *Self(void *L) { return static_cast<Group *>(Resolve(L, 1, Group::s_class)); }

using LooseBool_t = int(__fastcall *)(void *L, int idx, int defaultValue);
bool LooseBool(void *L, int idx, int dflt) {
    return reinterpret_cast<LooseBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, idx, dflt) != 0;
}

int __fastcall Script_Play(void *L) {
    if (Group *g = Self(L))
        g->Play(LooseBool(L, 2, 0));
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
int __fastcall Script_GetProgress(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushNumber(L, g->elapsed / g->duration);
    return 1;
}
int __fastcall Script_IsDone(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushBool(L, g->IsDone());
    return 1;
}
int __fastcall Script_IsPlaying(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushBool(L, g->IsPlaying());
    return 1;
}
int __fastcall Script_IsPaused(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushBool(L, (g->flags & GROUP_PAUSED) != 0);
    return 1;
}
int __fastcall Script_IsPendingFinish(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushBool(L, (g->flags & GROUP_PENDING_FINISH) != 0);
    return 1;
}
int __fastcall Script_GetDuration(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushNumber(L, g->duration);
    return 1;
}
int __fastcall Script_SetLooping(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetLooping(\"loopType\")", g->DisplayName());
        return 0;
    }
    LoopType type;
    if (StringToLoopType(Game::Lua::ToString(L, 2), type)) {
        g->loopType = type;
        return 0;
    }
    Game::Lua::Error(L, "%s:SetLooping(): Unknown loop type specified", g->DisplayName());
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
    Game::Lua::PushString(L, LoopStateToString(g->loopState));
    return 1;
}
int __fastcall Script_GetMaxOrder(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushNumber(L, g->GetMaxAnimOrder() + 1);
    return 1;
}
int __fastcall Script_SetInitialOffset(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: %s:SetInitialOffset(x, y)", g->DisplayName());
        return 0;
    }
    const float x = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 2)));
    const float y = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 3)));
    // 4.3.4's SetInitialOffset: a near-zero offset is zero.
    if (x * x + y * y <= 0.00000023841858f) {
        g->initialOffsetX = 0.0f;
        g->initialOffsetY = 0.0f;
    } else {
        g->initialOffsetX = x;
        g->initialOffsetY = y;
    }
    return 0;
}
int __fastcall Script_GetInitialOffset(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Game::InternalToPixel(g->initialOffsetX));
    Game::Lua::PushNumber(L, Game::InternalToPixel(g->initialOffsetY));
    return 2;
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
int __fastcall Script_SetIgnoreFramerateThrottle(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    if (LooseBool(L, 2, 0))
        g->flags |= GROUP_IGNORE_THROTTLE;
    else
        g->flags &= ~GROUP_IGNORE_THROTTLE;
    return 0;
}
int __fastcall Script_GetIgnoreFramerateThrottle(void *L) {
    Group *g = Self(L);
    if (g == nullptr)
        return 0;
    if (g->flags & GROUP_IGNORE_THROTTLE)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"GetObjectType", &Script_GetObjectType},
    {"IsObjectType", &Script_IsObjectType},
    {"GetName", &Script_GetName},
    {"GetParent", &Script_GetParent},
    {"Play", &Script_Play},
    {"Pause", &Script_Pause},
    {"Stop", &Script_Stop},
    {"Finish", &Script_Finish},
    {"GetProgress", &Script_GetProgress},
    {"IsDone", &Script_IsDone},
    {"IsPlaying", &Script_IsPlaying},
    {"IsPaused", &Script_IsPaused},
    {"IsPendingFinish", &Script_IsPendingFinish},
    {"GetDuration", &Script_GetDuration},
    {"SetLooping", &Script_SetLooping},
    {"GetLooping", &Script_GetLooping},
    {"GetLoopState", &Script_GetLoopState},
    {"GetMaxOrder", &Script_GetMaxOrder},
    {"SetInitialOffset", &Script_SetInitialOffset},
    {"GetInitialOffset", &Script_GetInitialOffset},
    {"GetAnimations", &Script_GetAnimations},
    {"CreateAnimation", &Script_CreateAnimation},
    {"HasScript", &Script_HasScript},
    {"GetScript", &Script_GetScript},
    {"SetScript", &Script_SetScript},
    {"HookScript", &Script_HookScript},
    {"SetIgnoreFramerateThrottle", &Script_SetIgnoreFramerateThrottle},
    {"GetIgnoreFramerateThrottle", &Script_GetIgnoreFramerateThrottle},
};

} // namespace

Class Group::s_class{"AnimationGroup", nullptr, kGroupScripts, 7, kMethods,
                     static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0])), 0, 0};

} // namespace Animation
