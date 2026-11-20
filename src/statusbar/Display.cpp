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

// `bar:SetTimerDuration(duration [, interpolation [, direction]])` /
// `GetTimerDuration()`, `bar:SetValue(value [, interpolation])`,
// `SetMinMaxValues(min, max [, interpolation])`, `GetInterpolatedValue()`,
// `IsInterpolating()`, `SetToTargetValue()`, `Enum.StatusBarTimerDirection`
// and `Enum.StatusBarInterpolation` (SimpleStatusBarAPIDocumentation.lua).
//
// The 3.80.2 client's CSimpleStatusBar (methods 0x14458ee10, OnFrameUpdate
// FUN_1428cd4c0, SetTimerDuration FUN_1428ccc50, the ease step
// FUN_1428cceb0), which a retail 12.1.5 probe agreed with (2026-11-01; the
// logs are in docs/notes/duration-timer.md):
//
// The timer:
//   - The bar fills by the duration's elapsed share (ElapsedTime) or its
//     remaining share (RemainingTime), in real time, every frame. After the
//     end it stays full or empty; the timer stays set. A zero duration draws
//     empty in either direction.
//   - The bar keeps the duration object, not a copy: a change made to the
//     object afterwards shows at the next frame. GetTimerDuration returns a
//     new Lua object for it each call, nil with no timer.
//   - The value and range read 0 while the timer runs, and it runs no
//     OnValueChanged or OnMinMaxChanged. SetValue on a timer bar does
//     nothing; SetMinMaxValues ends the timer and leaves the bar with no
//     value (MinMaxChanged.cpp). A zero duration is a timer too.
//   - A hidden bar does not move; it is right again on its first frame shown.
//
// ExponentialEaseOut:
//   - The ease works on the fill FRACTION: each frame,
//     shown = target - expf(-18 * elapsed) * (target - shown) (FUN_142a5c180
//     with 18.0f).
//   - When the gap before a frame's step is under 0.001 of the current
//     target, that frame shows the target and IsInterpolating turns false. A
//     timer's share moves each frame, so an eased timer keeps easing while
//     its share moves more than 0.001 a frame.
//   - SetValue stores the target at once: GetValue returns it, OnValueChanged
//     runs once, with it. Its interpolation applies even to an unchanged
//     value: ExponentialEaseOut starts an ease, Immediate stops one.
//     GetInterpolatedValue returns the shown value.
//   - SetMinMaxValues uses its interpolation only when the range changed and
//     the bar has a value (3.80.2 hands it to its re-apply of the value):
//     ExponentialEaseOut keeps the fraction shown and eases to the new one.
//   - SetToTargetValue ends an ease; without one it does nothing.
//   - A hidden bar does not ease; after Show it steps by each frame's elapsed.
//
// 1.12 has the split this needs: OnFrameUpdate draws the fill from the bar's
// GetFillFraction virtual (4.3.4's GetAnimValue), not from the stored value.
// A co-hook on it answers the shown fraction for a bar with a timer or an
// ease. StatusBar::Fill's layout takes its fraction there, so a timer or an
// ease keeps the bar's fill style. Advance runs from that same update
// pass, for shown bars only, with the pass's elapsed, which is the step the
// probe found; the fraction it settled is the one drawn.
//
// The timer's value and range are written straight into the bar, not through
// its setters: 3.80.2 replaces them with no script, and SetMinMaxValues would
// end the timer.
//
// SetValue and SetMinMaxValues stay the engine's methods, wrapped: the
// wrapper reads the interpolation argument and the fraction shown, calls the
// engine's method (its checks, its errors, its setter), then starts or stops
// the ease. Unlike 3.80.2, an interpolation argument that is not a valid
// enum reads Immediate rather than raising: stock 1.12 code passes stray
// arguments to these methods (see Wrap).

#include "statusbar/Display.h"

#include "Game.h"
#include "Offsets.h"
#include "duration/Object.h"
#include "frame/Destruction.h"
#include "lua/EnumArg.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace StatusBar::Display {

namespace {

namespace Lua = Game::Lua;

enum Direction : int {
    kElapsedTime = 0,
    kRemainingTime = 1,
};

enum Interpolation : int {
    kImmediate = 0,
    kExponentialEaseOut = 1,
};

constexpr float kEaseRate = 18.0f;     // per second, of the gap (DAT_14415c320)
constexpr float kEaseSnapGap = 0.001f; // of the fill fraction (DAT_14415b630)

const Lua::EnumIntegerEntry kDirectionEntries[] = {
    {"ElapsedTime", kElapsedTime},
    {"RemainingTime", kRemainingTime},
};

const Lua::EnumIntegerEntry kInterpolationEntries[] = {
    {"Immediate", kImmediate},
    {"ExponentialEaseOut", kExponentialEaseOut},
};

// A bar with a timer, an ease, or both. One with neither has no entry.
struct State {
    Duration::Object::Span *timer = nullptr; // one reference
    int direction = kElapsedTime;
    bool easing = false;
    float shown = 0.0f; // the fraction the fill draws
};

std::unordered_map<const void *, State> g_bars;

using GetFillFraction_t = float(__fastcall *)(void *bar, void *edx);
GetFillFraction_t g_origGetFillFraction = nullptr;

uint32_t &Flags(void *bar) { return Game::Ref<uint32_t>(bar, Offsets::OFF_VALUE_FLAGS); }

void MarkChanged(void *bar) { Flags(bar) |= Offsets::VALUE_FLAG_CHANGED; }

// A zero duration draws empty either way (FUN_1428ccd90).
float TimerShare(const State &s) {
    if (Duration::Object::IsZero(s.timer))
        return 0.0f;
    return static_cast<float>(s.direction == kRemainingTime ? Duration::Object::RemainingPercent(s.timer)
                                                            : Duration::Object::ElapsedPercent(s.timer));
}

// The fraction the bar heads for: its timer's share, or its stored value's.
float Target(void *bar, const State &s) {
    return s.timer != nullptr ? TimerShare(s) : g_origGetFillFraction(bar, nullptr);
}

// The fraction drawn now.
float Drawn(void *bar) {
    const auto it = g_bars.find(bar);
    return it != g_bars.end() ? it->second.shown : g_origGetFillFraction(bar, nullptr);
}

void EraseIfIdle(std::unordered_map<const void *, State>::iterator it) {
    if (it->second.timer == nullptr && !it->second.easing)
        g_bars.erase(it);
}

// Starts an ease from `from`, the fraction drawn before the change, or stops
// one and shows the target at once.
void SetEase(void *bar, int interpolation, float from) {
    if (interpolation == kExponentialEaseOut) {
        State &s = g_bars[bar];
        s.easing = true;
        s.shown = from;
    } else if (const auto it = g_bars.find(bar); it != g_bars.end()) {
        it->second.easing = false;
        it->second.shown = Target(bar, it->second);
        EraseIfIdle(it);
    }
    MarkChanged(bar);
}

float __fastcall GetFillFraction_h(void *bar, void *edx) {
    const auto it = g_bars.find(bar);
    return it != g_bars.end() ? it->second.shown : g_origGetFillFraction(bar, edx);
}

const Game::HookAutoRegister _fillFractionHook{Offsets::FUN_STATUSBAR_GET_FILL_FRACTION,
                                               reinterpret_cast<void *>(&GetFillFraction_h),
                                               reinterpret_cast<void **>(&g_origGetFillFraction)};

void OnFrameDestroyed(void *frame) {
    const auto it = g_bars.find(frame);
    if (it == g_bars.end())
        return;
    Duration::Object::Release(it->second.timer);
    g_bars.erase(it);
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

void *ResolveBar(void *L) { return Lua::ResolveTypedObject(L, 1, Offsets::VAR_STATUSBAR_LUA_TYPE_ID); }

bool HasTimerState(const void *bar) {
    const auto it = g_bars.find(bar);
    return it != g_bars.end() && it->second.timer != nullptr;
}

using ScriptFn_t = int(__fastcall *)(void *L);

// The interpolation argument of SetValue / SetMinMaxValues. Only
// ExponentialEaseOut eases; any other value past the engine's own arguments
// is Immediate, never an error. The stock methods ignore extra arguments, so
// vanilla code can pass one by accident - a second return value
// (`bar:SetValue(GetPetExperience())`) or a leftover flag - and must keep
// working.
int InterpolationArg(void *L, int idx) {
    return Lua::Type(L, idx) == Lua::TYPE_NUMBER && Lua::ToNumber(L, idx) == kExponentialEaseOut
               ? kExponentialEaseOut
               : kImmediate;
}

// `bar:SetValue(value [, interpolation])`: the engine's method between
// reading the fraction shown and setting the ease. A bad self or value raises
// the engine's own error. On a timer bar it changes nothing (3.80.2's
// FUN_1428ccb50 acts only on a range and value).
int __fastcall Script_SetValue(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    if (HasTimerState(bar)) {
        Lua::CheckNumber(L, 2);
        return 0;
    }
    const int interpolation = InterpolationArg(L, 3);
    const float from = Drawn(bar);
    reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_STATUSBAR_SET_VALUE)(L);
    SetEase(bar, interpolation, from);
    return 0;
}

// `bar:SetMinMaxValues(min, max [, interpolation])`: the interpolation
// applies only when the range changed and the bar keeps a value.
int __fastcall Script_SetMinMaxValues(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const int interpolation = InterpolationArg(L, 4);
    const float from = Drawn(bar);
    const float min = Game::Read<float>(bar, Offsets::OFF_VALUE_MIN);
    const float max = Game::Read<float>(bar, Offsets::OFF_STATUSBAR_MAX);
    reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_STATUSBAR_SET_MIN_MAX_VALUES)(L);
    const bool changed = Game::Read<float>(bar, Offsets::OFF_VALUE_MIN) != min ||
                         Game::Read<float>(bar, Offsets::OFF_STATUSBAR_MAX) != max;
    if (changed && (Flags(bar) & Offsets::VALUE_FLAG_VALUE_SET) != 0)
        SetEase(bar, interpolation, from);
    return 0;
}

// The shown value: the shown fraction across the range while easing, else
// the stored value.
int __fastcall Script_GetInterpolatedValue(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const float min = Game::Read<float>(bar, Offsets::OFF_VALUE_MIN);
    const float max = Game::Read<float>(bar, Offsets::OFF_STATUSBAR_MAX);
    const auto it = g_bars.find(bar);
    const bool easing = it != g_bars.end() && it->second.easing;
    Lua::PushNumber(L, easing ? min + it->second.shown * (max - min)
                              : Game::Read<float>(bar, Offsets::OFF_VALUE_VALUE));
    return 1;
}

int __fastcall Script_IsInterpolating(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_bars.find(bar);
    Lua::PushBool(L, it != g_bars.end() && it->second.easing);
    return 1;
}

int __fastcall Script_SetToTargetValue(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_bars.find(bar);
    if (it != g_bars.end() && it->second.easing)
        SetEase(bar, kImmediate, 0.0f);
    return 0;
}

int __fastcall Script_SetTimerDuration(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    Duration::Object::Span *span = Duration::Object::ToSpan(L, 2);
    int interpolation = kImmediate;
    int direction = kElapsedTime;
    if (span == nullptr || !Lua::ToEnumByte(L, 3, kExponentialEaseOut, interpolation) ||
        !Lua::ToEnumByte(L, 4, kRemainingTime, direction)) {
        Lua::Error(L, "Usage: %s:SetTimerDuration(duration [, interpolation [, direction]])",
                   Lua::ObjectDisplayName(bar));
        return 0;
    }
    const float from = Drawn(bar);
    State &state = g_bars[bar];
    Duration::Object::Retain(span);
    Duration::Object::Release(state.timer);
    state.timer = span;
    state.direction = direction;
    state.shown = TimerShare(state);

    Game::Ref<float>(bar, Offsets::OFF_VALUE_MIN) = 0.0f;
    Game::Ref<float>(bar, Offsets::OFF_STATUSBAR_MAX) = 0.0f;
    Game::Ref<float>(bar, Offsets::OFF_VALUE_VALUE) = 0.0f;
    Flags(bar) |= Offsets::VALUE_FLAG_RANGE_SET | Offsets::VALUE_FLAG_VALUE_SET;
    SetEase(bar, interpolation, from);
    return 0;
}

int __fastcall Script_GetTimerDuration(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_bars.find(bar);
    if (it != g_bars.end() && it->second.timer != nullptr)
        Duration::Object::Push(L, it->second.timer);
    else
        Lua::PushNil(L);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetValue", &Script_SetValue},
    {"SetMinMaxValues", &Script_SetMinMaxValues},
    {"GetInterpolatedValue", &Script_GetInterpolatedValue},
    {"IsInterpolating", &Script_IsInterpolating},
    {"SetToTargetValue", &Script_SetToTargetValue},
    {"SetTimerDuration", &Script_SetTimerDuration},
    {"GetTimerDuration", &Script_GetTimerDuration},
};

const Game::Doc::Field kSetValueArgs[] = {
    Game::Doc::Req("value", "number"),
    Game::Doc::Opt("interpolation", "StatusBarInterpolation", "\"Immediate\""),
};
const Game::Doc::Field kSetMinMaxValuesArgs[] = {
    Game::Doc::Req("minValue", "number"),
    Game::Doc::Req("maxValue", "number"),
    Game::Doc::Opt("interpolation", "StatusBarInterpolation", "\"Immediate\""),
};
const Game::Doc::Field kValueRets[] = {Game::Doc::Req("value", "number")};
const Game::Doc::Field kIsInterpolatingRets[] = {Game::Doc::Req("isInterpolating", "bool")};
const Game::Doc::Field kSetTimerDurationArgs[] = {
    Game::Doc::Req("duration", "LuaDurationObject", "The bar keeps this object and follows changes to it."),
    Game::Doc::Opt("interpolation", "StatusBarInterpolation", "\"Immediate\""),
    Game::Doc::Opt("direction", "StatusBarTimerDirection", "\"ElapsedTime\""),
};
const Game::Doc::Field kGetTimerDurationRets[] = {
    Game::Doc::Opt("duration", "LuaDurationObject", nullptr, "Nil when the bar has no timer."),
};

const Game::Doc::Function kSetValue{"Sets the bar's value, at once or eased.", kSetValueArgs, {}};
const Game::Doc::Function kSetMinMaxValues{"Sets the bar's range, at once or eased.", kSetMinMaxValuesArgs, {}};
const Game::Doc::Function kGetInterpolatedValue{"Returns the value the bar shows while it eases.", {},
                                                kValueRets};
const Game::Doc::Function kIsInterpolating{"Returns true while the bar eases toward its value.", {},
                                           kIsInterpolatingRets};
const Game::Doc::Function kSetToTargetValue{"Ends the bar's ease and shows its value.", {}, {}};
const Game::Doc::Function kSetTimerDuration{"Makes the bar fill by itself from a duration, every frame.",
                                            kSetTimerDurationArgs, {}};
const Game::Doc::Function kGetTimerDuration{"Returns the duration the bar's timer follows.", {},
                                            kGetTimerDurationRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetValue", &kSetValue},
    {"SetMinMaxValues", &kSetMinMaxValues},
    {"GetInterpolatedValue", &kGetInterpolatedValue},
    {"IsInterpolating", &kIsInterpolating},
    {"SetToTargetValue", &kSetToTargetValue},
    {"SetTimerDuration", &kSetTimerDuration},
    {"GetTimerDuration", &kGetTimerDuration},
};

void RegisterLuaFunctions() {
    Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_STATUSBAR_METHOD_REGISTRY), g_methods,
                              g_methodDocs);
    Lua::RegisterIntegerEnum(
        "Enum", "StatusBarTimerDirection", kDirectionEntries, "SimpleStatusBarConstants");
    Lua::RegisterIntegerEnum(
        "Enum", "StatusBarInterpolation", kInterpolationEntries, "SimpleStatusBarConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Advance(void *bar, float elapsed) {
    const auto it = g_bars.find(bar);
    if (it == g_bars.end())
        return;
    State &s = it->second;
    const float target = Target(bar, s);
    float shown = target;
    if (s.easing) {
        const float gap = target - s.shown;
        if (std::fabs(gap) < kEaseSnapGap)
            s.easing = false;
        else
            shown = target - std::exp(-(kEaseRate * elapsed)) * gap;
    }
    if (shown != s.shown) {
        s.shown = shown;
        MarkChanged(bar);
    }
    EraseIfIdle(it);
}

bool ClearTimer(void *bar) {
    const auto it = g_bars.find(bar);
    if (it == g_bars.end() || it->second.timer == nullptr)
        return false;
    Duration::Object::Release(it->second.timer);
    it->second.timer = nullptr;
    EraseIfIdle(it);
    return true;
}

bool HasTimer(const void *bar, bool &zero) {
    const auto it = g_bars.find(bar);
    if (it == g_bars.end() || it->second.timer == nullptr)
        return false;
    zero = Duration::Object::IsZero(it->second.timer);
    return true;
}

} // namespace StatusBar::Display
