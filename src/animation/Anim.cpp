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

// 4.3.4's CSimpleAnim and its Alpha, Translation, Scale and Rotation
// subclasses (wowdecomp15595 CSimpleAnim.cpp, CSimple*Anim.cpp and their
// _Scripts.cpp), on Animation::Object, and the group's CreateAnimation.

#include "animation/Classes.h"
#include "animation/Region.h"
#include "animation/Xml.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"

#include <cmath>
#include <cstring>

namespace Animation {

namespace {

const char *const kAnimScripts[] = {"onload", "onplay", "onpause", "onstop", "onfinished", "onupdate"};

Class &ClassOf(AnimKind kind) {
    switch (kind) {
    case KIND_ALPHA: return Anim::s_alphaClass;
    case KIND_TRANSLATION: return Anim::s_translationClass;
    case KIND_SCALE: return Anim::s_scaleClass;
    case KIND_ROTATION: return Anim::s_rotationClass;
    default: return Anim::s_class;
    }
}

} // namespace

AnimKind Anim::KindOf(const char *typeName) {
    if (typeName == nullptr || *typeName == '\0')
        return KIND_ANIMATION;
    if (Ascii::EqualCI(typeName, "Alpha"))
        return KIND_ALPHA;
    if (Ascii::EqualCI(typeName, "Translation"))
        return KIND_TRANSLATION;
    if (Ascii::EqualCI(typeName, "Scale"))
        return KIND_SCALE;
    if (Ascii::EqualCI(typeName, "Rotation"))
        return KIND_ROTATION;
    return KIND_ANIMATION;
}

// ---- Construction ---------------------------------------------------------------

// 4.3.4's constructor links the animation into the group's list; it joins an
// order list only once it has an order (NotifyAnimAdded, SortAnimations).
Anim::Anim(Group *group, AnimKind kind, const char *name) : Object(ClassOf(kind), group, name), kind(kind) {
    if (group != nullptr)
        group->animations.push_back(this);
}

Anim::~Anim() = default;

// ---- Playback --------------------------------------------------------------------

bool Anim::Play() {
    if (targetName[0] != '\0')
        target = Region::FindRegionByName(targetName);
    if (playState != 1) {
        playState = 1;
        RunScript(A_ONPLAY, 0);
    }
    return true;
}

void Anim::Pause() {
    GetGroup()->Pause();
    if (playState == 1) {
        playState = 2;
        RunScript(A_ONPAUSE, 0);
    }
}

void Anim::Stop(bool requested) {
    if (playState == 0)
        return;
    loopState = STATE_NONE;
    playState = 0;
    elapsed = 0.0f;
    progressWithDelay = 0.0f;
    progress = 0.0f;
    if (HasScript(A_ONSTOP)) {
        Game::Lua::PushBool(Game::Lua::State(), requested);
        RunScript(A_ONSTOP, 1);
    }
}

bool Anim::IsDelaying() const {
    if (progressWithDelay <= 0.0f || progressWithDelay >= 1.0f)
        return false;
    const float total = endDelay + duration + startDelay;
    if (std::fabs(total) < 0.00000023841858f)
        return false;
    if (loopState == STATE_REVERSE) {
        if (endDelay > elapsed)
            return true;
        if (endDelay + duration > elapsed)
            return false;
    } else {
        if (startDelay > elapsed)
            return true;
        if (duration + startDelay > elapsed)
            return false;
    }
    return total > elapsed;
}

float Anim::UpdateProgress(float elapsedSec) {
    const float prevElapsed = elapsed;
    elapsed += elapsedSec;
    const float total = orderPadding + startDelay + endDelay + duration;
    float now;
    if (total > elapsed) {
        now = elapsed;
        progressWithDelay = now / total;
        if (progressWithDelay > 1.0f)
            progressWithDelay = 1.0f;
        if (std::fabs(duration) < 0.00000023841858f) {
            progress = 1.0f;
        } else {
            frameElapsed += elapsedSec;
            if ((GetGroup()->flags & GROUP_IGNORE_THROTTLE) || frameElapsed >= kFrameInterval) {
                const float delay = loopState == STATE_REVERSE ? orderPadding + endDelay : startDelay;
                progress = (now - delay) / duration;
                if (progress < 0.0f)
                    progress = 0.0f;
                else if (progress >= 1.0f)
                    progress = 1.0f;
                frameElapsed = 0.0f;
            }
        }
    } else {
        now = total;
        progressWithDelay = 1.0f;
        progress = 1.0f;
    }
    float p = loopState == STATE_REVERSE ? 1.0f - progress : progress;
    if (hasSmoother) {
        // CSimpleAnimSCurveSmoother::GetSmoothProgress.
        if (std::fabs(smoothIn) >= 0.001f && std::fabs(smoothOut) >= 0.001f)
            p = 0.5f - std::cos(p * 3.1415927f) * 0.5f;
        else if (std::fabs(smoothIn) >= 0.001f)
            p = 1.0f - std::cos(p * 1.5707964f);
        else if (std::fabs(smoothOut) >= 0.001f)
            p = std::cos((p + 1.0f) * 1.5707964f) * -1.0f;
    }
    smoothProgress = p;
    return now - prevElapsed;
}

bool Anim::OnUpdate(float elapsedSec, float &consumed) {
    consumed = 0.0f;
    if (progressWithDelay >= 1.0f) {
        Apply(smoothProgress);
        return true;
    }
    if (playState == 0)
        return true;
    if (playState == 2) {
        Apply(smoothProgress);
        return false;
    }
    consumed = UpdateProgress(elapsedSec);
    if (HasScript(A_ONUPDATE)) {
        Game::Lua::PushNumber(Game::Lua::State(), elapsedSec);
        RunScript(A_ONUPDATE, 1);
    }
    Apply(smoothProgress);
    if (progressWithDelay < 1.0f)
        return false;
    RunScript(A_ONFINISHED, 0);
    return true;
}

void Anim::Apply(float p) {
    void *region = GetRegionParent();
    if (region == nullptr)
        return;
    switch (kind) {
    case KIND_ALPHA:
        Region::AddAnimAlpha(region, target, static_cast<int16_t>(change * p));
        break;
    case KIND_TRANSLATION:
        Region::AddAnimTranslation(region, target, offsetX * p, offsetY * p);
        break;
    case KIND_SCALE:
        Region::AddAnimScale(region, target, originPoint, originX, originY, scaleChangeX * p, scaleChangeY * p);
        break;
    case KIND_ROTATION:
        Region::AddAnimRotation(region, target, originPoint, originX, originY, radians * p);
        break;
    default:
        break;
    }
}

// ---- Settings ----------------------------------------------------------------------

void Anim::SetStartDelay(float delay, bool notify) {
    startDelay = delay < 0.0f ? 0.0f : delay;
    if (notify && parent != nullptr)
        GetGroup()->ComputeDuration();
}

void Anim::SetEndDelay(float delay, bool notify) {
    endDelay = delay < 0.0f ? 0.0f : delay;
    if (notify && parent != nullptr)
        GetGroup()->ComputeDuration();
}

void Anim::SetDuration(float value, bool notify) {
    duration = value;
    if (notify && parent != nullptr)
        GetGroup()->ComputeDuration();
}

void Anim::SetOrder(int value, bool notify, bool wrap) {
    const int oldOrder = order;
    if (wrap)
        order = value < 0 || value > 99 ? 0 : value;
    else
        order = value < 0 ? 0 : (value > 99 ? 99 : value);
    // 4.3.4 compares the old order with the argument, not the clamped value.
    if (notify && parent != nullptr && oldOrder != value)
        GetGroup()->NotifyOrderChanged(this, oldOrder);
}

void Anim::SetSmoothing(float in, float out) {
    if ((in > 0.0f && in <= 1.0f) || (out > 0.0f && out <= 1.0f)) {
        hasSmoother = true;
        smoothIn = in < 0.0f ? 0.0f : (in < 1.0f ? in : 1.0f);
        smoothOut = out < 0.0f ? 0.0f : (out < 1.0f ? out : 1.0f);
    }
}

bool Anim::GetSmoothing(float &in, float &out) const {
    in = hasSmoother ? smoothIn : 0.0f;
    out = hasSmoother ? smoothOut : 0.0f;
    return hasSmoother;
}

bool Anim::ChangeParent(Group *group) {
    if (group == nullptr || parent == group)
        return false;
    GetGroup()->NotifyAnimRemoved(this);
    parent = group;
    group->NotifyAnimAdded(this);
    return true;
}

void Anim::PostLoadXML() {
    flags &= ~1;
    RunScript(A_ONLOAD, 0);
}

// ---- Lua methods ----------------------------------------------------------------------

namespace {

using StringToPoint_t = int(__fastcall *)(const char *name, int *point);
using PointToString_t = const char *(__fastcall *)(int point);

Anim *Self(void *L) { return static_cast<Anim *>(Resolve(L, 1, Anim::s_class)); }
Anim *SelfOf(void *L, Class &cls) { return static_cast<Anim *>(Resolve(L, 1, cls)); }

// Usage error when argument `idx` isn't a number; the number otherwise.
bool NumberArg(void *L, int idx, Anim *anim, const char *usage) {
    if (Game::Lua::IsNumber(L, idx))
        return true;
    Game::Lua::Error(L, usage, anim->DisplayName());
    return false;
}

int __fastcall Script_Play(void *L) {
    if (Anim *a = Self(L))
        a->Play();
    return 0;
}
int __fastcall Script_Pause(void *L) {
    if (Anim *a = Self(L))
        a->Pause();
    return 0;
}
int __fastcall Script_Stop(void *L) {
    if (Anim *a = Self(L))
        a->Stop(true);
    return 0;
}

template <typename Fn> int PushBoolOf(void *L, Fn fn) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    Game::Lua::PushBool(L, fn(a));
    return 1;
}
template <typename Fn> int PushNumberOf(void *L, Fn fn) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, fn(a));
    return 1;
}

int __fastcall Script_IsDone(void *L) { return PushBoolOf(L, [](Anim *a) { return a->progressWithDelay >= 1.0f; }); }
int __fastcall Script_IsPlaying(void *L) { return PushBoolOf(L, [](Anim *a) { return a->playState == 1; }); }
int __fastcall Script_IsPaused(void *L) { return PushBoolOf(L, [](Anim *a) { return a->playState == 2; }); }
int __fastcall Script_IsStopped(void *L) { return PushBoolOf(L, [](Anim *a) { return a->playState == 0; }); }
int __fastcall Script_IsDelaying(void *L) { return PushBoolOf(L, [](Anim *a) { return a->IsDelaying(); }); }
int __fastcall Script_GetElapsed(void *L) { return PushNumberOf(L, [](Anim *a) { return a->elapsed; }); }
int __fastcall Script_GetStartDelay(void *L) { return PushNumberOf(L, [](Anim *a) { return a->startDelay; }); }
int __fastcall Script_GetEndDelay(void *L) { return PushNumberOf(L, [](Anim *a) { return a->endDelay; }); }
int __fastcall Script_GetDuration(void *L) { return PushNumberOf(L, [](Anim *a) { return a->duration; }); }
int __fastcall Script_GetSmoothProgress(void *L) {
    return PushNumberOf(L, [](Anim *a) { return a->smoothProgress; });
}
int __fastcall Script_GetProgress(void *L) { return PushNumberOf(L, [](Anim *a) { return a->progress; }); }
int __fastcall Script_GetProgressWithDelay(void *L) {
    return PushNumberOf(L, [](Anim *a) { return a->progressWithDelay; });
}
int __fastcall Script_GetOrder(void *L) {
    return PushNumberOf(L, [](Anim *a) { return static_cast<double>(a->order + 1); });
}

int __fastcall Script_SetStartDelay(void *L) {
    Anim *a = Self(L);
    if (a != nullptr && NumberArg(L, 2, a, "Usage: %s:SetStartDelay(delaySec)"))
        a->SetStartDelay(static_cast<float>(Game::Lua::ToNumber(L, 2)), true);
    return 0;
}
int __fastcall Script_SetEndDelay(void *L) {
    Anim *a = Self(L);
    if (a != nullptr && NumberArg(L, 2, a, "Usage: %s:SetEndDelay(delaySec)"))
        a->SetEndDelay(static_cast<float>(Game::Lua::ToNumber(L, 2)), true);
    return 0;
}
int __fastcall Script_SetDuration(void *L) {
    Anim *a = Self(L);
    if (a != nullptr && NumberArg(L, 2, a, "Usage: %s:SetDuration(durationSec)"))
        a->SetDuration(static_cast<float>(Game::Lua::ToNumber(L, 2)), true);
    return 0;
}
int __fastcall Script_SetSmoothProgress(void *L) {
    Anim *a = Self(L);
    if (a != nullptr && NumberArg(L, 2, a, "Usage: %s:SetSmoothProgress(smoothProgress)"))
        a->smoothProgress = static_cast<float>(Game::Lua::ToNumber(L, 2));
    return 0;
}
int __fastcall Script_SetOrder(void *L) {
    Anim *a = Self(L);
    if (a != nullptr && NumberArg(L, 2, a, "Usage: %s:SetOrder(order)"))
        a->SetOrder(static_cast<int>(Game::Lua::ToNumber(L, 2)) - 1, true, false);
    return 0;
}
int __fastcall Script_SetSmoothing(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetSmoothing(\"smoothingType\")", a->DisplayName());
        return 0;
    }
    float in, out;
    if (StringToSmoothWeights(Game::Lua::ToString(L, 2), in, out)) {
        a->SetSmoothing(in, out);
        return 0;
    }
    Game::Lua::Error(L, "%s:SetSmoothing(): Unknown smoothing type specified", a->DisplayName());
    return 0;
}
int __fastcall Script_GetSmoothing(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    float in, out;
    a->GetSmoothing(in, out);
    Game::Lua::PushString(L, SmoothWeightsToString(in, out));
    return 1;
}

int __fastcall Script_SetParent(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, "%s:SetParent(): Cannot set a 'nil' parent for animations", a->DisplayName());
        return 0;
    }
    Group *group = nullptr;
    if (Game::Lua::IsString(L, 2)) {
        // CScriptObject::GetScriptObjectByName(name, AnimationGroup).
        Game::Lua::PushString(L, Game::Lua::ToString(L, 2));
        Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
        void *object = Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE ? Game::Lua::ResolveObject(L, -1) : nullptr;
        Game::Lua::SetTop(L, -2);
        if (object != nullptr && IsOurs(object) &&
            Game::Lua::IsA(object, reinterpret_cast<uintptr_t>(&Group::s_class.typeId)))
            group = static_cast<Group *>(object);
    } else if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE) {
        void *object = Game::Lua::ResolveObject(L, 2);
        if (object == nullptr) {
            Game::Lua::Error(L, "%s:SetParent(): Couldn't find 'this' in parent object", a->DisplayName());
            return 0;
        }
        if (!IsOurs(object) || !Game::Lua::IsA(object, reinterpret_cast<uintptr_t>(&Group::s_class.typeId))) {
            Game::Lua::Error(L, "%s:SetParent(): Wrong parent object type, expected AnimationGroup", a->DisplayName());
            return 0;
        }
        group = static_cast<Group *>(object);
    }
    if (group == nullptr) {
        Game::Lua::Error(L, "%s:SetParent(): Couldn't find animation group named '%s'", a->DisplayName(),
                         Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : "");
        return 0;
    }
    const int oldOrder = a->order;
    if (Game::Lua::IsNumber(L, 3)) {
        a->SetOrder(static_cast<int>(Game::Lua::ToNumber(L, 3)) - 1, false, false);
    } else {
        int value = a->order;
        if (value == -1)
            value = group->GetMaxAnimOrder() + 1;
        a->SetOrder(value, false, true);
    }
    if (!a->ChangeParent(group) && oldOrder != a->order)
        group->NotifyOrderChanged(a, oldOrder);
    return 0;
}

int __fastcall Script_GetRegionParent(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    void *region = a->GetRegionParent();
    if (region != nullptr)
        Game::Lua::PushObject(L, region);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// ---- Alpha ----

int __fastcall Script_SetChange(void *L) {
    Anim *a = SelfOf(L, Anim::s_alphaClass);
    if (a == nullptr || !NumberArg(L, 2, a, "Usage: %s:SetChange(change)"))
        return 0;
    // CSimpleAlphaAnim::SetChange.
    const float change = static_cast<float>(Game::Lua::ToNumber(L, 2));
    const float value = change < -1.0f ? -1.0f : (change >= 1.0f ? 1.0f : change);
    a->change = static_cast<int16_t>(value * 255.0f);
    return 0;
}
int __fastcall Script_GetChange(void *L) {
    Anim *a = SelfOf(L, Anim::s_alphaClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, a->change * (1.0f / 255.0f));
    return 1;
}

// ---- Translation ----

int __fastcall Script_SetOffset(void *L) {
    Anim *a = SelfOf(L, Anim::s_translationClass);
    if (a == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: %s:SetOffset(x, y)", a->DisplayName());
        return 0;
    }
    const float x = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 2)));
    const float y = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 3)));
    // CSimpleTranslationAnim::SetOffset: a near-zero offset is zero.
    if (x * x + y * y <= 0.00000023841858f) {
        a->offsetX = 0.0f;
        a->offsetY = 0.0f;
    } else {
        a->offsetX = x;
        a->offsetY = y;
    }
    return 0;
}
int __fastcall Script_GetOffset(void *L) {
    Anim *a = SelfOf(L, Anim::s_translationClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Game::InternalToPixel(a->offsetX));
    Game::Lua::PushNumber(L, Game::InternalToPixel(a->offsetY));
    return 2;
}

// ---- Scale and Rotation origin (ReadOrigin) ----

Anim *OriginSelf(void *L) {
    Anim *a = static_cast<Anim *>(Resolve(L, 1, Anim::s_class));
    if (a != nullptr && a->kind != KIND_SCALE && a->kind != KIND_ROTATION) {
        // The type test of 4.3.4's Scale / Rotation SetOrigin.
        Game::Lua::Error(L, "Wrong object type for member function");
        return nullptr;
    }
    return a;
}
int __fastcall Script_SetOrigin(void *L) {
    Anim *a = OriginSelf(L);
    if (a == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2) || !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L, "Usage: %s:SetOrigin(point, offsetX, offsetY)", a->DisplayName());
        return 0;
    }
    int point = Offsets::FRAMEPOINT_CENTER;
    if (!reinterpret_cast<StringToPoint_t>(Offsets::FUN_STRING_TO_FRAME_POINT)(Game::Lua::ToString(L, 2), &point))
        point = Offsets::FRAMEPOINT_CENTER;
    a->originPoint = point;
    a->originX = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 3)));
    a->originY = Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 4)));
    return 0;
}
// 4.3.4 pushes the stored offsets as they are, in layout units.
int __fastcall Script_GetOrigin(void *L) {
    Anim *a = OriginSelf(L);
    if (a == nullptr)
        return 0;
    Game::Lua::PushString(L, reinterpret_cast<PointToString_t>(Offsets::FUN_FRAME_POINT_TO_STRING)(a->originPoint));
    Game::Lua::PushNumber(L, a->originX);
    Game::Lua::PushNumber(L, a->originY);
    return 3;
}

// ---- Scale ----

int __fastcall Script_SetScale(void *L) {
    Anim *a = SelfOf(L, Anim::s_scaleClass);
    if (a == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: %s:SetScale(x, y)", a->DisplayName());
        return 0;
    }
    // CSimpleScaleAnim::SetScale.
    const float x = static_cast<float>(Game::Lua::ToNumber(L, 2));
    const float y = static_cast<float>(Game::Lua::ToNumber(L, 3));
    a->scaleChangeX = 1.0f - (x >= 0.001f ? x : 0.001f);
    a->scaleChangeY = 1.0f - (y >= 0.001f ? y : 0.001f);
    return 0;
}
int __fastcall Script_GetScale(void *L) {
    Anim *a = SelfOf(L, Anim::s_scaleClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, 1.0f - a->scaleChangeX);
    Game::Lua::PushNumber(L, 1.0f - a->scaleChangeY);
    return 2;
}

// ---- Rotation ----

int __fastcall Script_SetDegrees(void *L) {
    Anim *a = SelfOf(L, Anim::s_rotationClass);
    if (a != nullptr && NumberArg(L, 2, a, "Usage: %s:SetDegrees(degrees)"))
        a->radians = static_cast<float>(Game::Lua::ToNumber(L, 2) * 0.017453292f);
    return 0;
}
int __fastcall Script_GetDegrees(void *L) {
    Anim *a = SelfOf(L, Anim::s_rotationClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, a->radians * 57.29578f);
    return 1;
}
int __fastcall Script_SetRadians(void *L) {
    Anim *a = SelfOf(L, Anim::s_rotationClass);
    if (a != nullptr && NumberArg(L, 2, a, "Usage: %s:SetRadians(radians)"))
        a->radians = static_cast<float>(Game::Lua::ToNumber(L, 2));
    return 0;
}
int __fastcall Script_GetRadians(void *L) {
    Anim *a = SelfOf(L, Anim::s_rotationClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, a->radians);
    return 1;
}

const Game::Lua::FrameMethodEntry kAnimMethods[] = {
    {"GetObjectType", &Script_GetObjectType},
    {"IsObjectType", &Script_IsObjectType},
    {"GetName", &Script_GetName},
    {"GetParent", &Script_GetParent},
    {"Play", &Script_Play},
    {"Pause", &Script_Pause},
    {"Stop", &Script_Stop},
    {"IsDone", &Script_IsDone},
    {"IsPlaying", &Script_IsPlaying},
    {"IsPaused", &Script_IsPaused},
    {"IsStopped", &Script_IsStopped},
    {"IsDelaying", &Script_IsDelaying},
    {"GetElapsed", &Script_GetElapsed},
    {"SetStartDelay", &Script_SetStartDelay},
    {"GetStartDelay", &Script_GetStartDelay},
    {"SetEndDelay", &Script_SetEndDelay},
    {"GetEndDelay", &Script_GetEndDelay},
    {"SetDuration", &Script_SetDuration},
    {"GetDuration", &Script_GetDuration},
    {"GetSmoothProgress", &Script_GetSmoothProgress},
    {"SetSmoothProgress", &Script_SetSmoothProgress},
    {"GetProgress", &Script_GetProgress},
    {"GetProgressWithDelay", &Script_GetProgressWithDelay},
    {"SetOrder", &Script_SetOrder},
    {"GetOrder", &Script_GetOrder},
    {"SetSmoothing", &Script_SetSmoothing},
    {"GetSmoothing", &Script_GetSmoothing},
    {"SetParent", &Script_SetParent},
    {"GetRegionParent", &Script_GetRegionParent},
    {"HasScript", &Script_HasScript},
    {"GetScript", &Script_GetScript},
    {"SetScript", &Script_SetScript},
    {"HookScript", &Script_HookScript},
};
const Game::Lua::FrameMethodEntry kAlphaMethods[] = {
    {"SetChange", &Script_SetChange},
    {"GetChange", &Script_GetChange},
};
const Game::Lua::FrameMethodEntry kTranslationMethods[] = {
    {"SetOffset", &Script_SetOffset},
    {"GetOffset", &Script_GetOffset},
};
const Game::Lua::FrameMethodEntry kScaleMethods[] = {
    {"SetOrigin", &Script_SetOrigin},
    {"GetOrigin", &Script_GetOrigin},
    {"SetScale", &Script_SetScale},
    {"GetScale", &Script_GetScale},
};
const Game::Lua::FrameMethodEntry kRotationMethods[] = {
    {"SetOrigin", &Script_SetOrigin},   {"GetOrigin", &Script_GetOrigin},   {"SetDegrees", &Script_SetDegrees},
    {"GetDegrees", &Script_GetDegrees}, {"SetRadians", &Script_SetRadians}, {"GetRadians", &Script_GetRadians},
};

template <size_t N> constexpr int Count(const Game::Lua::FrameMethodEntry (&)[N]) { return static_cast<int>(N); }

} // namespace

Class Anim::s_class{"Animation", nullptr, kAnimScripts, 6, kAnimMethods, Count(kAnimMethods), 0, 0};
Class Anim::s_alphaClass{"Alpha", &Anim::s_class, nullptr, 0, kAlphaMethods, Count(kAlphaMethods), 0, 0};
Class Anim::s_translationClass{"Translation", &Anim::s_class, nullptr, 0, kTranslationMethods,
                               Count(kTranslationMethods), 0, 0};
Class Anim::s_scaleClass{"Scale", &Anim::s_class, nullptr, 0, kScaleMethods, Count(kScaleMethods), 0, 0};
Class Anim::s_rotationClass{"Rotation", &Anim::s_class, nullptr, 0, kRotationMethods, Count(kRotationMethods), 0, 0};

// ---- AnimationGroup:CreateAnimation ------------------------------------------------------

int __fastcall Script_CreateAnimation(void *L) {
    Group *group = static_cast<Group *>(Resolve(L, 1, Group::s_class));
    if (group == nullptr)
        return 0;
    const char *type = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : "Animation";
    const char *name = Game::Lua::IsString(L, 3) ? Game::Lua::ToString(L, 3) : nullptr;
    const void *templateNode = nullptr;
    if (Game::Lua::Type(L, 4) == Game::Lua::TYPE_STRING) {
        const char *inherits = Game::Lua::ToString(L, 4);
        templateNode = Xml::Template(inherits);
        if (templateNode == nullptr) {
            Game::Lua::Error(L, "%s:CreateAnimation(): Couldn't find inherited node \"%s\"", group->DisplayName(),
                             inherits);
            return 0;
        }
    }
    Anim *anim = new Anim(group, Anim::KindOf(type), name);
    if (templateNode != nullptr) {
        Xml::LoadAnim(anim, templateNode, nullptr);
        anim->PostLoadXML();
    }
    if (anim->order == -1)
        anim->SetOrder(group->GetMaxAnimOrder(), false, true);
    group->NotifyAnimAdded(anim);
    Push(L, anim);
    return 1;
}

} // namespace Animation
