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
// `GetTimerDuration()`, `Enum.StatusBarTimerDirection` and
// `Enum.StatusBarInterpolation` (SimpleStatusBarAPIDocumentation.lua).
//
// Retail's behavior, probed in a 12.1.5 client (2026-11-01; the log is in
// docs/notes/duration-timer.md):
//
//   - The bar fills by the duration's elapsed share (ElapsedTime) or its
//     remaining share (RemainingTime), every frame. After the end it stays
//     full or empty; the timer stays set.
//   - The bar keeps the duration object, not a copy: a change made to the
//     object afterwards shows at the next frame. GetTimerDuration returns a
//     new Lua object for it each call.
//   - The value and range read 0 while the timer runs (SetTimerDuration on a
//     bar at 42 of 0..100 reads value 0, min 0, max 0), and it runs no
//     OnValueChanged, then or on any frame after. SetValue leaves the timer
//     running (and clamps to 0..0); SetMinMaxValues ends it, after which
//     GetTimerDuration returns nil. A zero duration is a timer too.
//   - A hidden bar does not move; it is right again on its first frame shown.
//
// 1.12 has the same split: OnFrameUpdate draws the fill from the bar's
// GetFillFraction virtual (4.3.4's GetAnimValue), not from the stored value.
// So the timer stores 0 for the value and range, as retail reports, and a
// co-hook on GetFillFraction answers the timer's share for a bar that has
// one. Every fill layout takes its fraction there - 1.12's texture crop and
// StatusBar::Fill's reversed, centered, turned and tiled layouts alike - so a
// timer keeps the bar's crop and fill style. Advance marks the fill changed
// on each update where the share moved, and the share it computed is the
// one drawn, so a frame never reads the clock twice.
//
// The value and range are written straight into the bar, not through its
// setters: retail runs no OnValueChanged for the drop to 0, and
// SetMinMaxValues would end the timer. Whether retail runs OnMinMaxChanged
// there was not probed; this runs none.
//
// ExponentialEaseOut is accepted and draws as Immediate: easing is not ported
// yet.

#include "statusbar/Timer.h"

#include "Game.h"
#include "Offsets.h"
#include "duration/Object.h"
#include "frame/Destruction.h"

#include <cstdint>
#include <iterator>
#include <unordered_map>

namespace StatusBar::Timer {

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

const Lua::EnumIntegerEntry kDirectionEntries[] = {
    {"ElapsedTime", kElapsedTime},
    {"RemainingTime", kRemainingTime},
};

const Lua::EnumIntegerEntry kInterpolationEntries[] = {
    {"Immediate", kImmediate},
    {"ExponentialEaseOut", kExponentialEaseOut},
};

struct State {
    Duration::Object::Span *span = nullptr;
    int direction = kElapsedTime;
    float fraction = 0.0f; // the share the fill draws
};

std::unordered_map<const void *, State> g_timers;

using GetFillFraction_t = float(__fastcall *)(void *bar, void *edx);
GetFillFraction_t g_origGetFillFraction = nullptr;

float Share(const State &s) {
    return static_cast<float>(s.direction == kRemainingTime ? Duration::Object::RemainingPercent(s.span)
                                                            : Duration::Object::ElapsedPercent(s.span));
}

uint32_t &Flags(void *bar) { return Game::Ref<uint32_t>(bar, Offsets::OFF_VALUE_FLAGS); }

float __fastcall GetFillFraction_h(void *bar, void *edx) {
    const auto it = g_timers.find(bar);
    return it != g_timers.end() ? it->second.fraction : g_origGetFillFraction(bar, edx);
}

const Game::HookAutoRegister _fillFractionHook{Offsets::FUN_STATUSBAR_GET_FILL_FRACTION,
                                               reinterpret_cast<void *>(&GetFillFraction_h),
                                               reinterpret_cast<void **>(&g_origGetFillFraction)};

void OnFrameDestroyed(void *frame) { Clear(frame); }

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

void *ResolveBar(void *L) { return Lua::ResolveTypedObject(L, 1, Offsets::VAR_STATUSBAR_LUA_TYPE_ID); }

// An optional enum argument: `dflt` when nil, else an integer in [0, max].
bool EnumArg(void *L, int idx, int max, int dflt, int &out) {
    out = dflt;
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    if (Lua::Type(L, idx) != Lua::TYPE_NUMBER)
        return false;
    const double n = Lua::ToNumber(L, idx);
    if (!(n >= 0 && n <= max) || n != static_cast<int>(n))
        return false;
    out = static_cast<int>(n);
    return true;
}

int __fastcall Script_SetTimerDuration(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    Duration::Object::Span *span = Duration::Object::ToSpan(L, 2);
    int interpolation, direction;
    if (span == nullptr || !EnumArg(L, 3, kExponentialEaseOut, kImmediate, interpolation) ||
        !EnumArg(L, 4, kRemainingTime, kElapsedTime, direction)) {
        Lua::Error(L, "Usage: %s:SetTimerDuration(duration [, interpolation [, direction]])",
                   Lua::ObjectDisplayName(bar));
        return 0;
    }
    State &state = g_timers[bar];
    Duration::Object::Retain(span);
    Duration::Object::Release(state.span);
    state.span = span;
    state.direction = direction;
    state.fraction = Share(state);

    Game::Ref<float>(bar, Offsets::OFF_VALUE_MIN) = 0.0f;
    Game::Ref<float>(bar, Offsets::OFF_STATUSBAR_MAX) = 0.0f;
    Game::Ref<float>(bar, Offsets::OFF_VALUE_VALUE) = 0.0f;
    Flags(bar) |= Offsets::VALUE_FLAG_RANGE_SET | Offsets::VALUE_FLAG_VALUE_SET | Offsets::VALUE_FLAG_CHANGED;
    return 0;
}

int __fastcall Script_GetTimerDuration(void *L) {
    void *bar = ResolveBar(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_timers.find(bar);
    if (it != g_timers.end())
        Duration::Object::Push(L, it->second.span);
    else
        Lua::PushNil(L);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetTimerDuration", &Script_SetTimerDuration},
    {"GetTimerDuration", &Script_GetTimerDuration},
};

const Game::Doc::Field kSetTimerDurationArgs[] = {
    Game::Doc::Req("duration", "LuaDurationObject", "The bar keeps this object and follows changes to it."),
    Game::Doc::Opt("interpolation", "StatusBarInterpolation", "\"Immediate\"", "Easing is not ported yet."),
    Game::Doc::Opt("direction", "StatusBarTimerDirection", "\"ElapsedTime\""),
};
const Game::Doc::Field kGetTimerDurationRets[] = {
    Game::Doc::Opt("duration", "LuaDurationObject", nullptr, "Nil when the bar has no timer."),
};

const Game::Doc::Function kSetTimerDuration{"Makes the bar fill by itself from a duration, every frame.",
                                            kSetTimerDurationArgs, {}};
const Game::Doc::Function kGetTimerDuration{"Returns the duration the bar's timer follows.", {},
                                            kGetTimerDurationRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetTimerDuration", &kSetTimerDuration},
    {"GetTimerDuration", &kGetTimerDuration},
};

void RegisterLuaFunctions() {
    Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_STATUSBAR_METHOD_REGISTRY), g_methods,
                              static_cast<int>(std::size(g_methods)), g_methodDocs,
                              static_cast<int>(std::size(g_methodDocs)));
    Lua::RegisterIntegerEnum("Enum", "StatusBarTimerDirection", kDirectionEntries,
                             static_cast<int>(std::size(kDirectionEntries)), "SimpleStatusBarConstants");
    Lua::RegisterIntegerEnum("Enum", "StatusBarInterpolation", kInterpolationEntries,
                             static_cast<int>(std::size(kInterpolationEntries)), "SimpleStatusBarConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Advance(void *bar) {
    const auto it = g_timers.find(bar);
    if (it == g_timers.end())
        return;
    const float share = Share(it->second);
    if (share != it->second.fraction) {
        it->second.fraction = share;
        Flags(bar) |= Offsets::VALUE_FLAG_CHANGED;
    }
}

void Clear(void *bar) {
    const auto it = g_timers.find(bar);
    if (it == g_timers.end())
        return;
    Duration::Object::Release(it->second.span);
    g_timers.erase(it);
}

} // namespace StatusBar::Timer
