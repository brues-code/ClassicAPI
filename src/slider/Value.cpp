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

// A slider's value as the later clients set it: `slider:SetValue(value [,
// treatAsMouseEvent])`, OnValueChanged's `userInput`, `SetObeyStepOnDrag` /
// `GetObeyStepOnDrag`, `SetStepsPerPage` / `GetStepsPerPage`,
// `IsDraggingThumb`, the XML `obeyStepOnDrag` / `stepsPerPage`, and paging.
//
// The later clients' CSimpleSlider::SetValue(value, userInput) (3.80.2
// 0x140591A60) differs from 1.12's FUN_SLIDER_SET_VALUE in four ways:
//  - it rounds to the step before it clamps, and not at all for a value at or
//    above the maximum, so the maximum is always reachable. 1.12 clamps
//    first, so a range that is not a whole number of steps rounds its top
//    end below the maximum, or past it;
//  - user input rounds only with obeyStepOnDrag;
//  - a value within 2^-22 of the stored one changes nothing and fires
//    nothing, where 1.12 tests exact equality, so a value that comes back
//    from a handler a rounding hair away fired OnValueChanged again; user
//    input always stores and fires;
//  - OnValueChanged gets `userInput` after the value.
// The hook replaces 1.12's function whole. The store and the fire are 1.12's:
// VALUE_FLAG_CHANGED | VALUE_FLAG_VALUE_SET, then the slot through the engine's
// variadic front with its "%f"; `userInput` is the trailing boolean
// (Frame::ScriptArgs). User input is told by the caller: OnMouseMove's
// cursor-to-value call, which the press also makes, is a drag; Script
// Slider:SetValue passes its third argument (treatAsMouseEvent). Every other
// caller (SetMinMaxValues, SetValueStep, the XML defaultValue, paging) is not.
//
// obeyStepOnDrag starts TRUE here, where the later clients' starts false:
// 1.12 rounds every drag and its stock option sliders rely on it (user
// decision 2026-11-19). Only sliders set away from the defaults get an entry.
//
// Paging (3.80.2 CSimpleSlider::OnLayerMouseDown 0x140591EE0): see Page.

#include "slider/Value.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/Destruction.h"
#include "frame/ScriptArgs.h"
#include "frame/ScriptSlots.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <intrin.h>
#include <unordered_map>

namespace Slider::Value {

namespace {

struct State {
    bool obeyStepOnDrag = true;
    int8_t stepsPerPage = 0;
};

// 3.80.2's Script SetStepsPerPage (0x140AA2C20) reads an int32, truncated,
// and stores only its low byte (+0x280); GetStepsPerPage (0x140AA1DD0) and
// the press (a signed `>= 1` test) read it sign-extended, so 200 reads back
// as -56 and pages nothing. `n` must be in the int32 range.
int8_t StepsPerPageByte(double n) { return static_cast<int8_t>(static_cast<int32_t>(n)); }

std::unordered_map<const void *, State> g_states;

State StateOf(const void *slider) {
    const auto it = g_states.find(slider);
    return it != g_states.end() ? it->second : State{};
}

// Stores `s`, dropping the entry when it is back to the defaults.
void Store(const void *slider, const State &s) {
    if (s.obeyStepOnDrag && s.stepsPerPage == 0)
        g_states.erase(slider);
    else
        g_states[slider] = s;
}

void OnFrameDestroyed(void *frame) { g_states.erase(frame); }

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

uint32_t &Flags(void *slider) { return Game::Ref<uint32_t>(slider, Offsets::OFF_VALUE_FLAGS); }

uint32_t *ValueChangedSlot(void *slider) {
    return Game::Ptr<uint32_t>(slider, Offsets::OFF_SLIDER_ONVALUECHANGED_SLOT);
}

// The OnValueChanged run in progress, for its trailing `userInput`.
struct FireScope {
    void *slider;
    bool userInput;
};
FireScope g_fire = {nullptr, false};

bool UserInput(int handlerRef, void *frame, bool *value) {
    if (frame == nullptr || frame != g_fire.slider ||
        static_cast<int>(ValueChangedSlot(frame)[0]) != handlerRef)
        return false;
    *value = g_fire.userInput;
    return true;
}

const Frame::ScriptArgs::AutoTrailingBool _userInput{&UserInput};

// 3.80.2 0x140591A60, over 1.12's fields.
void SetValue(void *slider, float value, bool userInput) {
    uint32_t &flags = Flags(slider);
    if ((flags & Offsets::VALUE_FLAG_RANGE_SET) == 0)
        return;
    const float min = Game::Read<float>(slider, Offsets::OFF_VALUE_MIN);
    const float max = min + Game::Read<float>(slider, Offsets::OFF_SLIDER_SPAN);
    const float step = Game::Read<float>(slider, Offsets::OFF_SLIDER_STEP);
    if ((!userInput || StateOf(slider).obeyStepOnDrag) && step != 0.0f && value < max) {
        float d = value - min;
        d = d <= 0.0f ? d - step * 0.5f : d + step * 0.5f;
        value = std::trunc(d * (1.0f / step)) * step + min;
    }
    if (!(min <= value)) // NaN included
        value = min;
    if (!(value <= max))
        value = max;
    const float old = Game::Read<float>(slider, Offsets::OFF_VALUE_VALUE);
    if ((flags & Offsets::VALUE_FLAG_VALUE_SET) != 0 && std::fabs(value - old) < 2.3841858e-07f && !userInput)
        return;
    Game::Ref<float>(slider, Offsets::OFF_VALUE_VALUE) = value;
    flags |= Offsets::VALUE_FLAG_CHANGED | Offsets::VALUE_FLAG_VALUE_SET;

    const FireScope saved = g_fire;
    g_fire = {slider, userInput};
    Frame::ScriptSlots::Fire(slider, ValueChangedSlot(slider), Offsets::VAR_SCRIPT_FMT_F, static_cast<double>(value));
    g_fire = saved;
}

// The Lua state Script Slider:SetValue runs in, while it runs.
void *g_scriptL = nullptr;

using ScriptFn_t = int(__fastcall *)(void *L);
ScriptFn_t g_origScriptSetValue = nullptr;

int __fastcall ScriptSetValue_h(void *L) {
    void *const saved = g_scriptL;
    g_scriptL = L;
    const int r = g_origScriptSetValue(L);
    g_scriptL = saved;
    return r;
}

using SetValue_t = void(__fastcall *)(void *slider, void *edx, float value);
SetValue_t g_origSetValue = nullptr; // never called: the hook replaces it

void __fastcall SetValue_h(void *slider, void *, float value) {
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    bool userInput = ret == Offsets::RET_SLIDER_DRAG_SET_VALUE;
    if (ret == Offsets::RET_SCRIPT_SLIDER_SET_VALUE && g_scriptL != nullptr)
        userInput = Game::Lua::ToBoolean(g_scriptL, 3) != 0;
    SetValue(slider, value, userInput);
}

const Game::HookAutoRegister _setValueHook{Offsets::FUN_SLIDER_SET_VALUE, reinterpret_cast<void *>(&SetValue_h),
                                           reinterpret_cast<void **>(&g_origSetValue)};
const Game::HookAutoRegister _scriptSetValueHook{Offsets::FUN_SCRIPT_SLIDER_SET_VALUE,
                                                 reinterpret_cast<void *>(&ScriptSetValue_h),
                                                 reinterpret_cast<void **>(&g_origScriptSetValue)};

// `obeyStepOnDrag` and `stepsPerPage`, read after the engine's Slider loader.
void OnSliderNode(void *slider, const void *node, void *) {
    State s = StateOf(slider);
    if (const char *obey = Xml::Attr(node, "obeyStepOnDrag"))
        s.obeyStepOnDrag = Game::StringToFlag(obey);
    if (const char *pages = Xml::Attr(node, "stepsPerPage")) {
        const double n = std::strtod(pages, nullptr);
        s.stepsPerPage = n > -2147483649.0 && n < 2147483648.0 ? StepsPerPageByte(n) : 0;
    }
    Store(slider, s);
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::SLIDER, &OnSliderNode};

void *ResolveSlider(void *L) { return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SLIDER_LUA_TYPE_ID); }

// 3.80.2's Script SetObeyStepOnDrag (0x140AA2840): a nil or missing argument
// is a usage error; any other value is read as Lua truth.
int __fastcall Script_SetObeyStepOnDrag(void *L) {
    void *slider = ResolveSlider(L);
    if (slider == nullptr)
        return 0;
    if (Game::Lua::Type(L, 2) <= Game::Lua::TYPE_NIL) {
        Game::Lua::Error(L, "Usage: %s:SetObeyStepOnDrag(obeyStepOnDrag)", Game::Lua::ObjectDisplayName(slider));
        return 0;
    }
    State s = StateOf(slider);
    s.obeyStepOnDrag = Game::Lua::ToBoolean(L, 2) != 0;
    Store(slider, s);
    return 0;
}

int __fastcall Script_GetObeyStepOnDrag(void *L) {
    void *slider = ResolveSlider(L);
    if (slider == nullptr)
        return 0;
    Game::Lua::PushBool(L, StateOf(slider).obeyStepOnDrag);
    return 1;
}

int __fastcall Script_SetStepsPerPage(void *L) {
    void *slider = ResolveSlider(L);
    if (slider == nullptr)
        return 0;
    const double n = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : 0.0;
    if (!Game::Lua::IsNumber(L, 2) || !(n > -2147483649.0 && n < 2147483648.0)) {
        Game::Lua::Error(L, "Usage: %s:SetStepsPerPage(stepsPerPage)", Game::Lua::ObjectDisplayName(slider));
        return 0;
    }
    State s = StateOf(slider);
    s.stepsPerPage = StepsPerPageByte(n);
    Store(slider, s);
    return 0;
}

int __fastcall Script_GetStepsPerPage(void *L) {
    void *slider = ResolveSlider(L);
    if (slider == nullptr)
        return 0;
    Game::Lua::PushNumber(L, StateOf(slider).stepsPerPage);
    return 1;
}

int __fastcall Script_IsDraggingThumb(void *L) {
    void *slider = ResolveSlider(L);
    if (slider == nullptr)
        return 0;
    Game::Lua::PushBool(L, (Flags(slider) & Offsets::VALUE_FLAG_DRAGGING) != 0);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetObeyStepOnDrag", &Script_SetObeyStepOnDrag},
    {"GetObeyStepOnDrag", &Script_GetObeyStepOnDrag},
    {"SetStepsPerPage", &Script_SetStepsPerPage},
    {"GetStepsPerPage", &Script_GetStepsPerPage},
    {"IsDraggingThumb", &Script_IsDraggingThumb},
};

const Game::Doc::Field kObeyArg[] = {Game::Doc::Req("obeyStepOnDrag", "bool")};
const Game::Doc::Field kObeyRet[] = {Game::Doc::Req("obeyStepOnDrag", "bool")};
const Game::Doc::Field kPagesArg[] = {Game::Doc::Req("stepsPerPage", "number")};
const Game::Doc::Field kPagesRet[] = {Game::Doc::Req("stepsPerPage", "number")};
const Game::Doc::Field kDraggingRet[] = {Game::Doc::Req("isDraggingThumb", "bool")};
const Game::Doc::Function kSetObeyStepOnDrag{
    "Sets whether a dragged value is rounded to the value step. It is true when the slider is created.", kObeyArg,
    {}};
const Game::Doc::Function kGetObeyStepOnDrag{"Returns whether a dragged value is rounded to the value step.", {},
                                             kObeyRet};
const Game::Doc::Function kSetStepsPerPage{
    "Sets how many value steps a click outside the thumb moves the value. 0, the default, makes the value jump "
    "to the cursor and starts a drag. The value is kept as a whole number from -128 to 127; a larger one wraps "
    "around (200 becomes -56).",
    kPagesArg, {}};
const Game::Doc::Function kGetStepsPerPage{"Returns how many value steps a click outside the thumb moves the value.",
                                           {}, kPagesRet};
const Game::Doc::Function kIsDraggingThumb{"Returns whether the mouse is dragging the slider.", {}, kDraggingRet};

const Game::Doc::Method g_methodDocs[] = {
    {"SetObeyStepOnDrag", &kSetObeyStepOnDrag}, {"GetObeyStepOnDrag", &kGetObeyStepOnDrag},
    {"SetStepsPerPage", &kSetStepsPerPage},     {"GetStepsPerPage", &kGetStepsPerPage},
    {"IsDraggingThumb", &kIsDraggingThumb},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_SLIDER_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// 3.80.2 0x140591EE0. A press inside the thumb's rect, or with stepsPerPage
// below 1 or no thumb, is not a page. Otherwise the value moves
// stepsPerPage steps toward the cursor: up the value for a press right of
// the thumb (horizontal) or below its top (vertical, the minimum at the
// top), down otherwise. It is not user input, so it rounds to the step.
bool Page(void *slider, const void *event) {
    const int pages = StateOf(slider).stepsPerPage;
    void *thumb = Game::Read<void *>(slider, Offsets::OFF_SLIDER_THUMB);
    if (pages < 1 || thumb == nullptr)
        return false;
    const float x = Game::Read<float>(event, Offsets::OFF_UI_MOUSE_EVENT_X);
    const float y = Game::Read<float>(event, Offsets::OFF_UI_MOUSE_EVENT_Y);
    const float *r = Game::Ptr<float>(thumb, Offsets::OFF_REGION_RECT); // {y, minX, y, maxX}
    const float minX = r[1];
    const float maxX = r[3];
    const float minY = std::fmin(r[0], r[2]);
    const float maxY = std::fmax(r[0], r[2]);
    if (minX <= x && x <= maxX && minY <= y && y <= maxY)
        return false;
    const bool vertical = Game::Read<uint32_t>(slider, Offsets::OFF_SLIDER_ORIENTATION) != 0;
    const int direction = vertical ? (maxY <= y ? -1 : 1) : (x <= maxX ? -1 : 1);
    SetValue(slider,
             static_cast<float>(direction * pages) * Game::Read<float>(slider, Offsets::OFF_SLIDER_STEP) +
                 Game::Read<float>(slider, Offsets::OFF_VALUE_VALUE),
             false);
    return true;
}

} // namespace Slider::Value
