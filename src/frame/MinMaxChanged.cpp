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

// The OnMinMaxChanged script of Slider and StatusBar.
//
// 4.3.4's SetMinMaxValues (CSimpleSlider.cpp:326, CSimpleStatusBar.cpp:141)
// returns when a range is set and the new one equals it; otherwise it stores
// the range, runs OnMinMaxChanged with (min, max), and then applies the
// current value again. The slider's test allows 2^-22 on the minimum and the
// span; the bar's is exact, after the bar takes max as the minimum when
// min > max. 1.12's SetMinMaxValues is the same store and re-apply with no
// test and no script.
//
// The co-hooks run the store with VALUE_FLAG_VALUE_SET held clear, which
// skips only the re-apply at its tail, then put the flag back, run the
// script, and do the re-apply as 1.12 does: the slider calls SetValue, the
// bar clears the flag and calls its SetValue slot. A call that leaves the
// range as it is goes straight to the original. Each class's XML range goes
// through the same setter after its <Scripts>, as in 4.3.4, so the script
// runs on load too.
//
// A bar leaving a timer (StatusBar::Display) takes the 3.80.2 client's path
// (FUN_1428cc920): the range is always new, so OnMinMaxChanged runs, and the
// bar is left with no value, so nothing re-applies one and the fill keeps
// its last drawing until a SetValue. Every other bar keeps 1.12's re-apply,
// which runs OnValueChanged on every range change: stock 1.12 code needs it
// (TextStatusBar refreshes its "value / max" text only on OnValueChanged,
// and UnitFrameHealthBar_Update sets the range, then a value that may not
// have moved).

#include "Game.h"
#include "Offsets.h"
#include "frame/ExtraScripts.h"
#include "frame/ScriptArgs.h"
#include "statusbar/Display.h"

#include <cmath>
#include <cstdint>

namespace Frame::MinMaxChanged {

namespace {

const Frame::ExtraScripts::Script g_onSliderMinMaxChanged{"onminmaxchanged", Offsets::VAR_SLIDER_LUA_TYPE_ID};
const Frame::ExtraScripts::Script g_onStatusBarMinMaxChanged{"onminmaxchanged",
                                                             Offsets::VAR_STATUSBAR_LUA_TYPE_ID};

using SetMinMax_t = void(__fastcall *)(void *frame, void *edx, float min, float max);
using SetValue_t = void(__fastcall *)(void *frame, void *edx, float value);

SetMinMax_t g_origSlider = nullptr;
SetMinMax_t g_origStatusBar = nullptr;

uint32_t &Flags(void *frame) { return *reinterpret_cast<uint32_t *>(static_cast<char *>(frame) + Offsets::OFF_VALUE_FLAGS); }

constexpr uint32_t kValueSet = Offsets::VALUE_FLAG_VALUE_SET;

// Stores the range through the original without its value re-apply, then
// runs the script with (min, max). Returns whether a value was set before.
bool StoreAndRun(SetMinMax_t orig, const Frame::ExtraScripts::Script &script, void *frame, void *edx, float min,
                 float max, float shownMin, float shownMax) {
    const bool hadValue = (Flags(frame) & kValueSet) != 0;
    Flags(frame) &= ~kValueSet;
    orig(frame, edx, min, max);
    if (hadValue)
        Flags(frame) |= kValueSet;

    Frame::ScriptArgs::RunSlot(script.Slot(frame), frame, [shownMin, shownMax](void *L) {
        Game::Lua::PushNumber(L, shownMin);
        Game::Lua::PushNumber(L, shownMax);
        return 2;
    });
    return hadValue;
}

void __fastcall SliderSetMinMax_h(void *slider, void *edx, float min, float max) {
    constexpr float kEpsilon = 0.00000023841858f;
    const float span = max - min;
    if ((Flags(slider) & Offsets::VALUE_FLAG_RANGE_SET) != 0 &&
        std::fabs(span - Game::Read<float>(slider, Offsets::OFF_SLIDER_SPAN)) < kEpsilon &&
        std::fabs(Game::Read<float>(slider, Offsets::OFF_VALUE_MIN) - min) < kEpsilon) {
        g_origSlider(slider, edx, min, max);
        return;
    }
    StoreAndRun(g_origSlider, g_onSliderMinMaxChanged, slider, edx, min, max, min, min + span);
    if ((Flags(slider) & kValueSet) != 0) // set before, or by the script
        reinterpret_cast<SetValue_t>(Offsets::FUN_SLIDER_SET_VALUE)(
            slider, edx, Game::Read<float>(slider, Offsets::OFF_VALUE_VALUE));
}

void __fastcall StatusBarSetMinMax_h(void *bar, void *edx, float min, float max) {
    const float storedMin = min > max ? max : min;
    if (StatusBar::Display::ClearTimer(bar)) {
        Flags(bar) &= ~kValueSet;
        StoreAndRun(g_origStatusBar, g_onStatusBarMinMaxChanged, bar, edx, min, max, storedMin, max);
        if ((Flags(bar) & kValueSet) != 0) // set by the script
            reinterpret_cast<SetValue_t>(Game::Read<uintptr_t>(*static_cast<void **>(bar),
                                                               Offsets::OFF_VMT_STATUSBAR_SET_VALUE))(
                bar, edx, Game::Read<float>(bar, Offsets::OFF_VALUE_VALUE));
        else // GetValue reads the minimum of a bar with no value
            *reinterpret_cast<float *>(static_cast<char *>(bar) + Offsets::OFF_VALUE_VALUE) =
                Game::Read<float>(bar, Offsets::OFF_VALUE_MIN);
        return;
    }
    if ((Flags(bar) & Offsets::VALUE_FLAG_RANGE_SET) != 0 &&
        Game::Read<float>(bar, Offsets::OFF_VALUE_MIN) == storedMin &&
        Game::Read<float>(bar, Offsets::OFF_STATUSBAR_MAX) == max) {
        g_origStatusBar(bar, edx, min, max);
        return;
    }
    const bool hadValue = StoreAndRun(g_origStatusBar, g_onStatusBarMinMaxChanged, bar, edx, min, max, storedMin, max);
    if (hadValue || (Flags(bar) & kValueSet) != 0) { // set before, or by the script
        if (hadValue)
            Flags(bar) &= ~kValueSet; // 1.12's tail: the re-apply always runs OnValueChanged
        const auto setValue = reinterpret_cast<SetValue_t>(
            Game::Read<uintptr_t>(*static_cast<void **>(bar), Offsets::OFF_VMT_STATUSBAR_SET_VALUE));
        setValue(bar, edx, Game::Read<float>(bar, Offsets::OFF_VALUE_VALUE));
    }
}

const Game::HookAutoRegister _sliderHook{Offsets::FUN_SLIDER_SET_MIN_MAX_VALUES,
                                         reinterpret_cast<void *>(&SliderSetMinMax_h),
                                         reinterpret_cast<void **>(&g_origSlider)};
const Game::HookAutoRegister _statusBarHook{Offsets::FUN_STATUSBAR_SET_MIN_MAX_VALUES,
                                            reinterpret_cast<void *>(&StatusBarSetMinMax_h),
                                            reinterpret_cast<void **>(&g_origStatusBar)};

} // namespace

} // namespace Frame::MinMaxChanged
