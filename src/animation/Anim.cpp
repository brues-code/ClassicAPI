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
// Path and its control points are in Path.cpp.
//
// Later clients' additions, on the same objects:
//   - the target setters (SetTarget, SetTargetName, SetTargetKey,
//     SetChildKey, SetTargetParent, GetTarget);
//   - Alpha's fromAlpha / toAlpha, which replaced 4.3.4's relative change:
//     the target's alpha becomes from + (to - from) x progress, kept as
//     bytes as 3.80.2 keeps them, an unset end 0;
//   - Scale's SetScaleFrom / SetScaleTo. 4.3.4's scale is the case from = 1,
//     and SetScale sets the end, so its old results do not change.
//
// The Lua methods and their arguments follow the 3.80.2 client's tables
// (0x14458F560 and the subclasses'), read as its generated bindings read
// them (Args.h).

#include "animation/Args.h"
#include "animation/Classes.h"
#include "animation/Region.h"
#include "animation/Xml.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/Layout.h"
#include "frame/DebugName.h"
#include "frame/ParentKey.h"
#include "lua/MixinTable.h"
#include "tick/FrameTick.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Animation {

namespace {

const char *const kAnimScripts[] = {"onload", "onplay", "onpause", "onstop", "onfinished", "onupdate"};

Class &ClassOf(AnimKind kind) {
    switch (kind) {
    case KIND_ALPHA: return Anim::s_alphaClass;
    case KIND_TRANSLATION: return Anim::s_translationClass;
    case KIND_SCALE: return Anim::s_scaleClass;
    case KIND_ROTATION: return Anim::s_rotationClass;
    case KIND_PATH: return Anim::s_pathClass;
    case KIND_VERTEX_COLOR: return Anim::s_vertexColorClass;
    case KIND_FLIPBOOK: return Anim::s_flipBookClass;
    case KIND_TEXCOORD: return Anim::s_texCoordClass;
    case KIND_LINE_TRANSLATION: return Anim::s_lineTranslationClass;
    case KIND_LINE_SCALE: return Anim::s_lineScaleClass;
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
    if (Ascii::EqualCI(typeName, "Path"))
        return KIND_PATH;
    if (Ascii::EqualCI(typeName, "VertexColor"))
        return KIND_VERTEX_COLOR;
    if (Ascii::EqualCI(typeName, "FlipBook"))
        return KIND_FLIPBOOK;
    if (Ascii::EqualCI(typeName, "TextureCoord"))
        return KIND_TEXCOORD;
    if (Ascii::EqualCI(typeName, "LineTranslation"))
        return KIND_LINE_TRANSLATION;
    if (Ascii::EqualCI(typeName, "LineScale"))
        return KIND_LINE_SCALE;
    return KIND_ANIMATION;
}

AnimKind Anim::KindOfTag(const char *tag) {
    if (tag != nullptr && Ascii::EqualCI(tag, "TextureCoordTranslation"))
        return KIND_TEXCOORD;
    if (tag != nullptr && Ascii::EqualCI(tag, "TextureCoord"))
        return KIND_ANIMATION;
    return KindOf(tag);
}

// ---- Construction ---------------------------------------------------------------

// 4.3.4's constructor links the animation into the group's list; it joins an
// order list only once it has an order (NotifyAnimAdded, SortAnimations).
Anim::Anim(Group *group, AnimKind kind, const char *name) : Object(ClassOf(kind), group, name), kind(kind) {
    if (group != nullptr)
        group->animations.push_back(this);
}

// ~CSimplePathAnim: a path's control points go with it.
Anim::~Anim() {
    for (ControlPoint *point : controlPoints)
        delete point;
}

// ---- Playback --------------------------------------------------------------------

void Anim::ResolveTarget() {
    if (!targetKey.empty())
        target = Region::FindRegionByKey(this, targetKey.c_str());
    else if (targetName[0] != '\0')
        target = Region::FindRegionByName(targetName);
}

bool Anim::Play() {
    ResolveTarget();
    if (playState != 1) {
        playState = 1;
        RunScript(A_ONPLAY, 0);
    }
    return true;
}

void Anim::Pause() {
    if (parent != nullptr)
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
            if ((GetGroup()->flags & GROUP_IGNORE_THROTTLE) || frameElapsed >= g_frameInterval ||
                elapsedSec < 0.0f) {
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

bool Anim::OnUpdate(float elapsedSec, float rawSec, float &consumed) {
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
        Game::Lua::PushNumber(Game::Lua::State(), rawSec);
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
        if (alphaRelative)
            Region::AddAnimAlpha(region, target, static_cast<int16_t>(change * p));
        else
            Region::SetAnimAlpha(region, target, static_cast<uint8_t>(fromAlpha + (toAlpha - fromAlpha) * p));
        break;
    case KIND_TRANSLATION:
        Region::AddAnimTranslation(region, target, offsetX * p, offsetY * p);
        break;
    case KIND_SCALE: {
        // AddAnimScale takes the share of the way to the origin, 1 - scale.
        const float x = scaleFromX + (scaleToX - scaleFromX) * p;
        const float y = scaleFromY + (scaleToY - scaleFromY) * p;
        Region::AddAnimScale(region, target, originPoint, originX, originY, 1.0f - x, 1.0f - y);
        break;
    }
    case KIND_ROTATION:
        Region::AddAnimRotation(region, target, originPoint, originX, originY, radians * p);
        break;
    case KIND_PATH:
        ApplyPath(p);
        break;
    case KIND_VERTEX_COLOR: {
        // 3.80.2's per-byte blend: t = p x 255 truncated; 0 is the start, 255
        // the end, else start + ((end - start) x t >> 8).
        const int t = static_cast<int>(p * 255.0f) & 0xFF;
        uint32_t color = t == 0 ? startColor : endColor;
        if (t != 0 && t != 255) {
            color = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const int a = static_cast<int>((startColor >> shift) & 0xFF);
                const int b = static_cast<int>((endColor >> shift) & 0xFF);
                color |= static_cast<uint32_t>((a + (((b - a) * t) >> 8)) & 0xFF) << shift;
            }
        }
        Region::SetAnimVertexColor(region, target, color);
        break;
    }
    case KIND_FLIPBOOK:
        if (!InStartDelay())
            Region::SetAnimFlipBookFrame(region, target, flipRows, flipColumns, flipFrames, flipFrameWidth,
                                         flipFrameHeight, p);
        break;
    case KIND_TEXCOORD:
        Region::AddAnimTexCoord(region, target, texOffsetU * p, texOffsetV * p);
        break;
    case KIND_LINE_TRANSLATION:
        Region::AddAnimLineTranslation(region, target, offsetX * p, offsetY * p);
        break;
    case KIND_LINE_SCALE: {
        const float x = scaleFromX + (scaleToX - scaleFromX) * p;
        const float y = scaleFromY + (scaleToY - scaleFromY) * p;
        Region::AddAnimLineScale(region, target, originPoint, originX, originY, 1.0f - x, 1.0f - y);
        break;
    }
    default:
        break;
    }
}

bool Anim::InStartDelay() const {
    if (playState == 0 || startDelay <= 0.0f)
        return false;
    if (loopState == STATE_REVERSE)
        return elapsed > orderPadding + endDelay + duration;
    return elapsed < startDelay;
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
    duration = value < 0.0f ? 0.0f : value;
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
    if (parent != nullptr)
        GetGroup()->NotifyAnimRemoved(this);
    parent = group;
    group->NotifyAnimAdded(this);
    return true;
}

void Anim::Mirror() {
    const float total = orderPadding + startDelay + endDelay + duration;
    elapsed = total - elapsed;
    if (elapsed < 0.0f)
        elapsed = 0.0f;
    progressWithDelay = 1.0f - progressWithDelay;
    progress = 1.0f - progress;
}

namespace {

std::vector<Anim *> g_released;

// The released animations go at the next UI frame: no script of theirs is
// running then.
void FreeReleased() {
    if (g_released.empty())
        return;
    const std::vector<Anim *> list = std::move(g_released);
    g_released.clear();
    for (Anim *anim : list)
        delete anim;
}

const Tick::FrameTick::AutoSubscribe _freeReleased{&FreeReleased};

// Released animations that outlived a /reload: their tables were in the old
// Lua state, so they go without touching it.
void DropReleased() {
    Object::s_detached = true;
    for (Anim *anim : g_released)
        delete anim;
    Object::s_detached = false;
    g_released.clear();
}

const Game::ReloadAutoRegister _reloadReg{&DropReleased};
const Game::GlueBootAutoRegister _glueBootReg{&DropReleased};

} // namespace

void Anim::Release(Anim *anim) {
    anim->Stop(false);
    anim->parent = nullptr;
    anim->target = nullptr;
    g_released.push_back(anim);
}

void Anim::PostLoadXML() {
    flags &= ~1;
    if (kind == KIND_PATH) {
        // CSimplePathAnim::PostLoadXML: the points' orders and positions.
        BuildSpline();
        UpdateControlPointPositions();
    }
    RunScript(A_ONLOAD, 0);
}

/// ---- Lua methods ----------------------------------------------------------------------

namespace {

Anim *Self(void *L) { return static_cast<Anim *>(Resolve(L, 1, Anim::s_class)); }

// The name an error gives an object: ours carry it, the engine's answer it.
const char *DisplayNameOf(void *object) {
    return IsOurs(object) ? static_cast<Object *>(object)->DisplayName() : Game::Lua::ObjectDisplayName(object);
}

Anim *SelfOf(void *L, Class &cls) { return static_cast<Anim *>(Resolve(L, 1, cls)); }

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
// 3.80.2's 0x140AA9ED0: stopped first when it is not, then Play's body. The
// group is not touched.
int __fastcall Script_Restart(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    a->Stop(true);
    a->Play();
    return 0;
}
int __fastcall Script_SetPlaying(void *L) {
    Anim *a = Self(L);
    bool play;
    if (a == nullptr || !Args::Bool(L, 2, "Usage: self:SetPlaying(play)", play))
        return 0;
    if (play)
        a->Play();
    else
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
// The stored order, 0..99 (3.80.2 adds nothing to it).
int __fastcall Script_GetOrder(void *L) { return PushNumberOf(L, [](Anim *a) { return a->order; }); }

// SetStartDelay / SetEndDelay / SetDuration(sec [, recomputeGroupDuration]):
// a negative value is 0; the group's duration is recomputed unless the
// second argument is false.
void SetTiming(void *L, void (Anim::*set)(float, bool), const char *usage) {
    Anim *a = Self(L);
    float value;
    if (a != nullptr && Args::Number(L, 2, usage, value))
        (a->*set)(value, Args::OptBool(L, 3, true));
}
int __fastcall Script_SetStartDelay(void *L) {
    SetTiming(L, &Anim::SetStartDelay, "Usage: self:SetStartDelay(delaySec [, recomputeGroupDuration])");
    return 0;
}
int __fastcall Script_SetEndDelay(void *L) {
    SetTiming(L, &Anim::SetEndDelay, "Usage: self:SetEndDelay(delaySec [, recomputeGroupDuration])");
    return 0;
}
int __fastcall Script_SetDuration(void *L) {
    SetTiming(L, &Anim::SetDuration, "Usage: self:SetDuration(durationSec [, recomputeGroupDuration])");
    return 0;
}
int __fastcall Script_SetSmoothProgress(void *L) {
    Anim *a = Self(L);
    float value;
    if (a != nullptr && Args::Number(L, 2, "Usage: self:SetSmoothProgress(durationSec)", value))
        a->smoothProgress = value;
    return 0;
}
// 0..99, and only while the animation has a group (3.80.2's 0x140AAA600).
int __fastcall Script_SetOrder(void *L) {
    Anim *a = Self(L);
    int value;
    if (a != nullptr && Args::Integer(L, 2, "Usage: self:SetOrder(newOrder)", value) && a->parent != nullptr)
        a->SetOrder(value, true, false);
    return 0;
}
// A weight pair that is all 0 ("NONE") leaves the smoothing as it is.
int __fastcall Script_SetSmoothing(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    constexpr char kUsage[] = "Usage: self:SetSmoothing(weights)";
    const char *text = Args::String(L, 2, kUsage);
    float in, out;
    if (text == nullptr)
        return 0;
    if (!StringToSmoothWeights(text, in, out)) {
        Args::Fail(L, 2, kUsage);
        return 0;
    }
    a->SetSmoothing(in, out);
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

// SetParent(parent [, order]): a group or a group's global name. The order,
// when given, is 0..99.
int __fastcall Script_SetParent(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    const char *name = a->Name();
    if (Args::Missing(L, 2)) {
        Game::Lua::RaiseClientMethodError(L, name, "SetParent", "Cannot set a 'nil' parent");
        return 0;
    }
    const uintptr_t groupType = reinterpret_cast<uintptr_t>(&Group::s_class.typeId);
    char message[Game::Lua::kClientErrorSize];
    Group *group = nullptr;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE) {
        void *object = Game::Lua::ResolveObject(L, 2);
        if (object == nullptr || !IsOurs(object) || !Game::Lua::IsA(object, groupType)) {
            std::snprintf(message, sizeof message, "Invalid object for SetParent() call. (%s)",
                          object != nullptr ? DisplayNameOf(object) : "<unnamed>");
            Game::Lua::RaiseClientPendingError(L, name, "SetParent", message);
            return 0;
        }
        group = static_cast<Group *>(object);
    } else if (Game::Lua::IsString(L, 2)) {
        // CScriptObject::GetScriptObjectByName(name, AnimationGroup).
        const char *parentName = Game::Lua::ToString(L, 2);
        Game::Lua::PushString(L, parentName);
        Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
        void *object = Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE ? Game::Lua::ResolveObject(L, -1) : nullptr;
        Game::Lua::SetTop(L, -2);
        if (object != nullptr && IsOurs(object) && Game::Lua::IsA(object, groupType))
            group = static_cast<Group *>(object);
        if (group == nullptr) {
            std::snprintf(message, sizeof message, "Couldn't find object named '%s'", parentName);
            Game::Lua::RaiseClientMethodError(L, name, "SetParent", message);
            return 0;
        }
    } else {
        Game::Lua::RaiseClientPendingError(L, name, "SetParent", "Invalid object for SetParent() call. (<unnamed>)");
        return 0;
    }
    const int oldOrder = a->order;
    int order = a->order;
    if (!Args::Missing(L, 3) && !Args::Integer(L, 3, "Usage: self:SetParent(parent [, order])", order))
        return 0;
    a->SetOrder(order, false, false);
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

// ---- Target ----
//
// An animation with no target animates its group's region. Each setter
// replaces the others' target; a name or key is looked up again on Play, as
// 4.3.4 looks up `target`. The name and key setters always succeed (3.80.2's
// 0x140AAB3A0 / 0x140AAB130 / 0x140AA9FD0); SetTarget raises an error for an
// object that is not a region.

int PushTrue(void *L) {
    Game::Lua::PushBool(L, true);
    return 1;
}

int __fastcall Script_SetTarget(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    constexpr char kUsage[] = "Usage: local success = self:SetTarget(target)";
    void *object = Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE ? Game::Lua::ResolveObject(L, 2) : nullptr;
    if (object == nullptr) {
        Args::Fail(L, 2, kUsage);
        return 0;
    }
    if (IsOurs(object) || !Game::Lua::IsA(object, Offsets::VAR_REGION_LUA_TYPE_ID)) {
        char message[Game::Lua::kClientErrorSize];
        std::snprintf(message, sizeof message, "Error setting target %s", DisplayNameOf(object));
        Game::Lua::RaiseClientPendingError(L, a->Name(), "SetTarget", message);
        return 0;
    }
    a->targetName[0] = '\0';
    a->targetKey.clear();
    a->target = object;
    return PushTrue(L);
}
int __fastcall Script_SetTargetName(void *L) {
    Anim *a = Self(L);
    const char *name = a != nullptr ? Args::String(L, 2, "Usage: local success = self:SetTargetName(name)") : nullptr;
    if (name == nullptr)
        return 0;
    std::snprintf(a->targetName, sizeof(a->targetName), "%s", CreateName(name, a).c_str());
    a->targetKey.clear();
    a->target = nullptr;
    a->ResolveTarget();
    return PushTrue(L);
}
int __fastcall Script_SetTargetKey(void *L) {
    Anim *a = Self(L);
    const char *key = a != nullptr ? Args::String(L, 2, "Usage: local success = self:SetTargetKey(key)") : nullptr;
    if (key == nullptr)
        return 0;
    a->targetName[0] = '\0';
    a->targetKey = key;
    a->target = nullptr;
    a->ResolveTarget();
    return PushTrue(L);
}
int __fastcall Script_SetChildKey(void *L) {
    Anim *a = Self(L);
    const char *key =
        a != nullptr ? Args::String(L, 2, "Usage: local success = self:SetChildKey(childKey)") : nullptr;
    if (key == nullptr)
        return 0;
    a->targetName[0] = '\0';
    a->targetKey = std::string("$parent.$parent.") + key;
    a->target = nullptr;
    a->ResolveTarget();
    return PushTrue(L);
}
int __fastcall Script_SetTargetParent(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    a->targetName[0] = '\0';
    a->targetKey.clear();
    a->target = a->GetRegionParent();
    return PushTrue(L);
}
int __fastcall Script_GetTarget(void *L) {
    Anim *a = Self(L);
    if (a == nullptr)
        return 0;
    a->ResolveTarget();
    void *region = a->target != nullptr ? a->target : a->GetRegionParent();
    if (region != nullptr)
        Game::Lua::PushObject(L, region);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// ---- Alpha ----
//
// 0..1, clamped without an error, kept as a byte (truncated): 0.5 reads
// back as 127/255.

void SetAlphaEnd(void *L, uint8_t Anim::*end, const char *usage) {
    Anim *a = SelfOf(L, Anim::s_alphaClass);
    float value;
    if (a == nullptr || !Args::Number(L, 2, usage, value))
        return;
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    a->*end = static_cast<uint8_t>(static_cast<int>(value * 255.0f));
    a->alphaRelative = false;
}
int __fastcall Script_SetFromAlpha(void *L) {
    SetAlphaEnd(L, &Anim::fromAlpha, "Usage: self:SetFromAlpha(normalizedAlpha)");
    return 0;
}
int __fastcall Script_SetToAlpha(void *L) {
    SetAlphaEnd(L, &Anim::toAlpha, "Usage: self:SetToAlpha(normalizedAlpha)");
    return 0;
}
int GetAlphaEnd(void *L, uint8_t Anim::*end) {
    Anim *a = SelfOf(L, Anim::s_alphaClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, (a->*end) / 255.0);
    return 1;
}
int __fastcall Script_GetFromAlpha(void *L) { return GetAlphaEnd(L, &Anim::fromAlpha); }

// SetChange / GetChange: 4.3.4's relative change, -1..1, kept for addons
// that call it. SetFromAlpha / SetToAlpha go back to the absolute alpha.
int __fastcall Script_SetChange(void *L) {
    Anim *a = SelfOf(L, Anim::s_alphaClass);
    float change;
    if (a == nullptr || !Args::Number(L, 2, "Usage: self:SetChange(change)", change))
        return 0;
    change = change < -1.0f ? -1.0f : (change >= 1.0f ? 1.0f : change);
    a->change = static_cast<int16_t>(change * 255.0f);
    a->alphaRelative = true;
    return 0;
}
int __fastcall Script_GetChange(void *L) {
    Anim *a = SelfOf(L, Anim::s_alphaClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, a->change / 255.0);
    return 1;
}
int __fastcall Script_GetToAlpha(void *L) { return GetAlphaEnd(L, &Anim::toAlpha); }

// ---- Translation ----

int __fastcall Script_SetOffset(void *L) {
    Anim *a = SelfOf(L, Anim::s_translationClass);
    if (a == nullptr)
        return 0;
    constexpr char kUsage[] = "Usage: self:SetOffset(offsetX, offsetY)";
    float x, y;
    if (!Args::RawNumber(L, 2, kUsage, x) || !Args::RawNumber(L, 3, kUsage, y))
        return 0;
    x = Game::PixelToInternal(x);
    y = Game::PixelToInternal(y);
    // A near-zero offset is zero.
    const bool zero = x * x + y * y <= 0.00000023841858f;
    a->offsetX = zero ? 0.0f : x;
    a->offsetY = zero ? 0.0f : y;
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

// ---- Scale and Rotation origin ----

Anim *OriginSelf(void *L) {
    Anim *a = static_cast<Anim *>(Resolve(L, 1, Anim::s_class));
    if (a != nullptr && a->kind != KIND_SCALE && a->kind != KIND_LINE_SCALE && a->kind != KIND_ROTATION) {
        Game::Lua::Error(L, "Wrong object type for member function");
        return nullptr;
    }
    return a;
}
// SetOrigin(point, x, y): the usage string's `origin` is two numbers. They
// are UI units here, as the XML <Origin> reads them.
int __fastcall Script_SetOrigin(void *L) {
    Anim *a = OriginSelf(L);
    if (a == nullptr)
        return 0;
    constexpr char kUsage[] = "Usage: self:SetOrigin(point, origin)";
    const char *pointName = Args::String(L, 2, kUsage);
    if (pointName == nullptr)
        return 0;
    int point = Offsets::FRAMEPOINT_CENTER;
    if (!Frame::Layout::StringToPoint(pointName, &point)) {
        Args::Fail(L, 2, kUsage);
        return 0;
    }
    float x, y;
    if (!Args::RawNumber(L, 3, kUsage, x) || !Args::RawNumber(L, 4, kUsage, y))
        return 0;
    a->originPoint = point;
    a->originX = Game::PixelToInternal(x);
    a->originY = Game::PixelToInternal(y);
    return 0;
}
int __fastcall Script_GetOrigin(void *L) {
    Anim *a = OriginSelf(L);
    if (a == nullptr)
        return 0;
    Game::Lua::PushString(L, Frame::Layout::PointToString(a->originPoint));
    Game::Lua::PushNumber(L, Game::InternalToPixel(a->originX));
    Game::Lua::PushNumber(L, Game::InternalToPixel(a->originY));
    return 3;
}

// ---- Scale ----
//
// Each value is at least 0.001. SetScale scales from 1 (from / to off);
// SetScaleFrom and SetScaleTo turn from / to on. GetScale answers 0, 0 while
// from / to is on, GetScaleFrom 0, 0 while it is off (3.80.2's 0x140AB1080 /
// 0x140AB11B0).

bool ScaleArgs(void *L, Anim *&a, const char *usage, float &x, float &y) {
    a = SelfOf(L, Anim::s_scaleClass);
    if (a == nullptr || !Args::RawNumber(L, 2, usage, x) || !Args::RawNumber(L, 3, usage, y))
        return false;
    x = x >= 0.001f ? x : 0.001f;
    y = y >= 0.001f ? y : 0.001f;
    return true;
}
int __fastcall Script_SetScale(void *L) {
    Anim *a;
    float x, y;
    if (!ScaleArgs(L, a, "Usage: self:SetScale(scale)", x, y))
        return 0;
    a->scaleFromTo = false;
    a->scaleFromX = a->scaleFromY = 1.0f;
    a->scaleToX = x;
    a->scaleToY = y;
    return 0;
}
int __fastcall Script_SetScaleFrom(void *L) {
    Anim *a;
    float x, y;
    if (!ScaleArgs(L, a, "Usage: self:SetScaleFrom(scale)", x, y))
        return 0;
    a->scaleFromTo = true;
    a->scaleFromX = x;
    a->scaleFromY = y;
    return 0;
}
int __fastcall Script_SetScaleTo(void *L) {
    Anim *a;
    float x, y;
    if (!ScaleArgs(L, a, "Usage: self:SetScaleTo(scale)", x, y))
        return 0;
    a->scaleFromTo = true;
    a->scaleToX = x;
    a->scaleToY = y;
    return 0;
}
int PushScale(void *L, bool show, float x, float y) {
    Game::Lua::PushNumber(L, show ? x : 0.0f);
    Game::Lua::PushNumber(L, show ? y : 0.0f);
    return 2;
}
int __fastcall Script_GetScale(void *L) {
    Anim *a = SelfOf(L, Anim::s_scaleClass);
    return a != nullptr ? PushScale(L, !a->scaleFromTo, a->scaleToX, a->scaleToY) : 0;
}
int __fastcall Script_GetScaleFrom(void *L) {
    Anim *a = SelfOf(L, Anim::s_scaleClass);
    return a != nullptr ? PushScale(L, a->scaleFromTo, a->scaleFromX, a->scaleFromY) : 0;
}
int __fastcall Script_GetScaleTo(void *L) {
    Anim *a = SelfOf(L, Anim::s_scaleClass);
    return a != nullptr ? PushScale(L, true, a->scaleToX, a->scaleToY) : 0;
}

// ---- Rotation ----

int __fastcall Script_SetDegrees(void *L) {
    Anim *a = SelfOf(L, Anim::s_rotationClass);
    float angle;
    if (a != nullptr && Args::Number(L, 2, "Usage: self:SetDegrees(angle)", angle))
        a->radians = angle * 0.017453292f;
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
    float angle;
    if (a != nullptr && Args::Number(L, 2, "Usage: self:SetRadians(angle)", angle))
        a->radians = angle;
    return 0;
}
int __fastcall Script_GetRadians(void *L) {
    Anim *a = SelfOf(L, Anim::s_rotationClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, a->radians);
    return 1;
}

// ---- VertexColor (3.80.2's table 0x1445908B0) ----

// A colour table with all four of r, g, b and a, each a finite number
// (FUN_140A6D740), clamped to 0..1 and kept as BGRA.
bool ColorArg(void *L, int idx, const char *usage, uint32_t &bgra) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_TABLE)
        return Args::Fail(L, idx, usage);
    const char *const keys[] = {"b", "g", "r", "a"};
    uint32_t packed = 0;
    for (int i = 0; i < 4; ++i) {
        Game::Lua::PushString(L, keys[i]);
        Game::Lua::GetTable(L, idx);
        const bool number = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER;
        const double v = number ? Game::Lua::ToNumber(L, -1) : 0.0;
        Game::Lua::SetTop(L, -2);
        if (!number || !std::isfinite(v))
            return Args::Fail(L, idx, usage);
        const double c = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        packed |= static_cast<uint32_t>(std::lround(c * 255.0)) << (i * 8);
    }
    bgra = packed;
    return true;
}

// A new ColorMixin, as 3.80.2's getters (0x140AC7E30 / 0x140AC7B60) build one:
// the stored bytes through 0x140AD1900 (lua/MixinTable.h).
int PushColor(void *L, uint32_t bgra) {
    Game::Lua::MixinTable::PushColor(L, bgra);
    return 1;
}

void SetColorEnd(void *L, uint32_t Anim::*end, const char *usage) {
    Anim *a = SelfOf(L, Anim::s_vertexColorClass);
    uint32_t bgra;
    if (a != nullptr && ColorArg(L, 2, usage, bgra))
        a->*end = bgra;
}
int __fastcall Script_SetStartColor(void *L) {
    SetColorEnd(L, &Anim::startColor, "Usage: self:SetStartColor(color)");
    return 0;
}
int __fastcall Script_SetEndColor(void *L) {
    SetColorEnd(L, &Anim::endColor, "Usage: self:SetEndColor(color)");
    return 0;
}
int __fastcall Script_GetStartColor(void *L) {
    Anim *a = SelfOf(L, Anim::s_vertexColorClass);
    return a != nullptr ? PushColor(L, a->startColor) : 0;
}
int __fastcall Script_GetEndColor(void *L) {
    Anim *a = SelfOf(L, Anim::s_vertexColorClass);
    return a != nullptr ? PushColor(L, a->endColor) : 0;
}

// ---- FlipBook (3.80.2's table 0x14458F8D0) ----
//
// Each value is a number from 0 to 4294967295, truncated. 0 is accepted
// here; a grid with no rows or columns draws nothing.

void SetFlipValue(void *L, uint32_t Anim::*field, const char *usage) {
    Anim *a = SelfOf(L, Anim::s_flipBookClass);
    if (a == nullptr)
        return;
    if (!Game::Lua::IsNumber(L, 2)) {
        Args::Fail(L, 2, usage);
        return;
    }
    const double v = Game::Lua::ToNumber(L, 2);
    if (!(v >= 0.0 && v <= 4294967295.0)) {
        char detail[96];
        std::snprintf(detail, sizeof detail, "outside of expected range %u to %u", 0u, 4294967295u);
        Args::Fail(L, 2, usage, detail);
        return;
    }
    a->*field = static_cast<uint32_t>(v);
}
int GetFlipValue(void *L, uint32_t Anim::*field) {
    Anim *a = SelfOf(L, Anim::s_flipBookClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(a->*field));
    return 1;
}
int __fastcall Script_SetFlipBookRows(void *L) {
    SetFlipValue(L, &Anim::flipRows, "Usage: self:SetFlipBookRows(rows)");
    return 0;
}
int __fastcall Script_SetFlipBookColumns(void *L) {
    SetFlipValue(L, &Anim::flipColumns, "Usage: self:SetFlipBookColumns(columns)");
    return 0;
}
int __fastcall Script_SetFlipBookFrames(void *L) {
    SetFlipValue(L, &Anim::flipFrames, "Usage: self:SetFlipBookFrames(frames)");
    return 0;
}
int __fastcall Script_SetFlipBookFrameWidth(void *L) {
    SetFlipValue(L, &Anim::flipFrameWidth, "Usage: self:SetFlipBookFrameWidth(width)");
    return 0;
}
int __fastcall Script_SetFlipBookFrameHeight(void *L) {
    SetFlipValue(L, &Anim::flipFrameHeight, "Usage: self:SetFlipBookFrameHeight(height)");
    return 0;
}
int __fastcall Script_GetFlipBookRows(void *L) { return GetFlipValue(L, &Anim::flipRows); }
int __fastcall Script_GetFlipBookColumns(void *L) { return GetFlipValue(L, &Anim::flipColumns); }
int __fastcall Script_GetFlipBookFrames(void *L) { return GetFlipValue(L, &Anim::flipFrames); }
int __fastcall Script_GetFlipBookFrameWidth(void *L) { return GetFlipValue(L, &Anim::flipFrameWidth); }
int __fastcall Script_GetFlipBookFrameHeight(void *L) { return GetFlipValue(L, &Anim::flipFrameHeight); }

// ---- TextureCoordTranslation (3.80.2's table 0x144590890) ----

int __fastcall Script_SetTexCoordOffset(void *L) {
    Anim *a = SelfOf(L, Anim::s_texCoordClass);
    if (a == nullptr)
        return 0;
    constexpr char kUsage[] = "Usage: self:SetOffset(offsetU, offsetV)";
    float u, v;
    if (!Args::Number(L, 2, kUsage, u) || !Args::Number(L, 3, kUsage, v))
        return 0;
    const bool zero = u * u + v * v <= 0.00000023841858f;
    a->texOffsetU = zero ? 0.0f : u;
    a->texOffsetV = zero ? 0.0f : v;
    return 0;
}
int __fastcall Script_GetTexCoordOffset(void *L) {
    Anim *a = SelfOf(L, Anim::s_texCoordClass);
    if (a == nullptr)
        return 0;
    Game::Lua::PushNumber(L, a->texOffsetU);
    Game::Lua::PushNumber(L, a->texOffsetV);
    return 2;
}

const Game::Lua::FrameMethodEntry kAnimMethods[] = {
    {"GetObjectType", &Script_GetObjectType},
    {"IsObjectType", &Script_IsObjectType},
    {"GetName", &Script_GetName},
    {"GetParent", &Script_GetParent},
    {"GetDebugName", &Frame::DebugName::Script_GetDebugName},
    {"GetParentKey", &Frame::ParentKey::Script_GetParentKey},
    {"SetParentKey", &Frame::ParentKey::Script_SetParentKey},
    {"ClearParentKey", &Frame::ParentKey::Script_ClearParentKey},
    {"GetDuration", &Script_GetDuration},
    {"GetElapsed", &Script_GetElapsed},
    {"GetEndDelay", &Script_GetEndDelay},
    {"GetOrder", &Script_GetOrder},
    {"GetProgress", &Script_GetProgress},
    {"GetRegionParent", &Script_GetRegionParent},
    {"GetScript", &Script_GetScript},
    {"GetSmoothProgress", &Script_GetSmoothProgress},
    {"GetSmoothing", &Script_GetSmoothing},
    {"GetStartDelay", &Script_GetStartDelay},
    {"GetTarget", &Script_GetTarget},
    {"HasScript", &Script_HasScript},
    {"HookScript", &Script_HookScript},
    {"IsDelaying", &Script_IsDelaying},
    {"IsDone", &Script_IsDone},
    {"IsPaused", &Script_IsPaused},
    {"IsPlaying", &Script_IsPlaying},
    {"IsStopped", &Script_IsStopped},
    {"Pause", &Script_Pause},
    {"Play", &Script_Play},
    {"Restart", &Script_Restart},
    {"SetChildKey", &Script_SetChildKey},
    {"SetDuration", &Script_SetDuration},
    {"SetEndDelay", &Script_SetEndDelay},
    {"SetOrder", &Script_SetOrder},
    {"SetParent", &Script_SetParent},
    {"SetPlaying", &Script_SetPlaying},
    {"SetScript", &Script_SetScript},
    {"SetSmoothProgress", &Script_SetSmoothProgress},
    {"SetSmoothing", &Script_SetSmoothing},
    {"SetStartDelay", &Script_SetStartDelay},
    {"SetTarget", &Script_SetTarget},
    {"SetTargetKey", &Script_SetTargetKey},
    {"SetTargetName", &Script_SetTargetName},
    {"SetTargetParent", &Script_SetTargetParent},
    {"Stop", &Script_Stop},
};
const Game::Lua::FrameMethodEntry kAlphaMethods[] = {
    {"GetChange", &Script_GetChange},
    {"SetChange", &Script_SetChange},
    {"GetFromAlpha", &Script_GetFromAlpha},
    {"GetToAlpha", &Script_GetToAlpha},
    {"SetFromAlpha", &Script_SetFromAlpha},
    {"SetToAlpha", &Script_SetToAlpha},
};
const Game::Lua::FrameMethodEntry kTranslationMethods[] = {
    {"GetOffset", &Script_GetOffset},
    {"SetOffset", &Script_SetOffset},
};
const Game::Lua::FrameMethodEntry kScaleMethods[] = {
    {"GetOrigin", &Script_GetOrigin},       {"GetScale", &Script_GetScale},
    {"GetScaleFrom", &Script_GetScaleFrom}, {"GetScaleTo", &Script_GetScaleTo},
    {"SetOrigin", &Script_SetOrigin},       {"SetScale", &Script_SetScale},
    {"SetScaleFrom", &Script_SetScaleFrom}, {"SetScaleTo", &Script_SetScaleTo},
};
const Game::Lua::FrameMethodEntry kRotationMethods[] = {
    {"GetDegrees", &Script_GetDegrees}, {"GetOrigin", &Script_GetOrigin},   {"GetRadians", &Script_GetRadians},
    {"SetDegrees", &Script_SetDegrees}, {"SetOrigin", &Script_SetOrigin},   {"SetRadians", &Script_SetRadians},
};

const Game::Lua::FrameMethodEntry kVertexColorMethods[] = {
    {"GetEndColor", &Script_GetEndColor},
    {"GetStartColor", &Script_GetStartColor},
    {"SetEndColor", &Script_SetEndColor},
    {"SetStartColor", &Script_SetStartColor},
};
const Game::Lua::FrameMethodEntry kFlipBookMethods[] = {
    {"GetFlipBookColumns", &Script_GetFlipBookColumns},
    {"GetFlipBookFrameHeight", &Script_GetFlipBookFrameHeight},
    {"GetFlipBookFrameWidth", &Script_GetFlipBookFrameWidth},
    {"GetFlipBookFrames", &Script_GetFlipBookFrames},
    {"GetFlipBookRows", &Script_GetFlipBookRows},
    {"SetFlipBookColumns", &Script_SetFlipBookColumns},
    {"SetFlipBookFrameHeight", &Script_SetFlipBookFrameHeight},
    {"SetFlipBookFrameWidth", &Script_SetFlipBookFrameWidth},
    {"SetFlipBookFrames", &Script_SetFlipBookFrames},
    {"SetFlipBookRows", &Script_SetFlipBookRows},
};
const Game::Lua::FrameMethodEntry kTexCoordMethods[] = {
    {"GetOffset", &Script_GetTexCoordOffset},
    {"SetOffset", &Script_SetTexCoordOffset},
};

template <size_t N> constexpr int Count(const Game::Lua::FrameMethodEntry (&)[N]) { return static_cast<int>(N); }

} // namespace

Class Anim::s_class{"Animation", nullptr, kAnimScripts, 6, kAnimMethods, Count(kAnimMethods), 0, 0};
Class Anim::s_alphaClass{"Alpha", &Anim::s_class, nullptr, 0, kAlphaMethods, Count(kAlphaMethods), 0, 0};
Class Anim::s_translationClass{"Translation", &Anim::s_class, nullptr, 0, kTranslationMethods,
                               Count(kTranslationMethods), 0, 0};
Class Anim::s_scaleClass{"Scale", &Anim::s_class, nullptr, 0, kScaleMethods, Count(kScaleMethods), 0, 0};
Class Anim::s_rotationClass{"Rotation", &Anim::s_class, nullptr, 0, kRotationMethods, Count(kRotationMethods), 0, 0};
Class Anim::s_vertexColorClass{"VertexColor", &Anim::s_class, nullptr, 0, kVertexColorMethods,
                               Count(kVertexColorMethods), 0, 0};
Class Anim::s_flipBookClass{"FlipBook", &Anim::s_class, nullptr, 0, kFlipBookMethods, Count(kFlipBookMethods), 0, 0};
Class Anim::s_texCoordClass{"TextureCoordTranslation", &Anim::s_class, nullptr, 0, kTexCoordMethods,
                            Count(kTexCoordMethods), 0, 0};
// LineTranslation and LineScale have no methods of their own: Translation's
// and Scale's (3.80.2 shares their tables).
Class Anim::s_lineTranslationClass{"LineTranslation", &Anim::s_translationClass, nullptr, 0, nullptr, 0, 0, 0};
Class Anim::s_lineScaleClass{"LineScale", &Anim::s_scaleClass, nullptr, 0, nullptr, 0, 0, 0};

// ---- AnimationGroup:CreateAnimation ------------------------------------------------------
//
// 3.80.2's 0x140AAD020 / FUN_1405A7B50: three optional strings; the type
// names any case, an unknown one is a plain Animation. The new animation is
// at order 0, as its constructor leaves it.

int __fastcall Script_CreateAnimation(void *L) {
    Group *group = static_cast<Group *>(Resolve(L, 1, Group::s_class));
    if (group == nullptr)
        return 0;
    constexpr char kUsage[] = "Usage: local anim = self:CreateAnimation([animationType, name, templateName])";
    bool ok;
    const char *type = Args::OptString(L, 2, kUsage, ok);
    if (!ok)
        return 0;
    const char *name = Args::OptString(L, 3, kUsage, ok);
    if (!ok)
        return 0;
    const char *inherits = Args::OptString(L, 4, kUsage, ok);
    if (!ok)
        return 0;
    const void *templateNode = nullptr;
    if (inherits != nullptr) {
        templateNode = Xml::Template(inherits);
        if (templateNode == nullptr) {
            char message[Game::Lua::kClientErrorSize];
            std::snprintf(message, sizeof message, "Couldn't find inherited node \"%s\"", inherits);
            Game::Lua::RaiseClientPendingError(L, group->Name(), "CreateAnimation", message);
            return 0;
        }
    }
    Anim *anim = new Anim(group, Anim::KindOf(type != nullptr ? type : "Animation"), name);
    if (templateNode != nullptr) {
        Xml::LoadAnim(anim, templateNode, nullptr);
        anim->PostLoadXML();
    }
    group->NotifyAnimAdded(anim);
    Push(L, anim);
    return 1;
}

} // namespace Animation
