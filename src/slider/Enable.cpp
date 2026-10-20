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

// `slider:Enable()`, `slider:Disable()` and `slider:IsEnabled()`.
//
// 4.3.4's Script Enable / Disable set or clear frame flag 0x400 and run
// OnEnable / OnDisable on every call, changed or not, unless the frame is
// loading - CSimpleFrame::RunOnEnableScript, which Frame::EnableScripts
// mirrors. The loading window (PreLoadXML to PostLoadXML) closes before
// OnLoad, so no Lua call can land in it. IsEnabled reads the flag.
//
// The flag's one reader is CSimpleSlider::OnLayerMouseDown: a disabled
// slider starts no drag and takes no value from the press, but the base
// frame's mouse-down - drag arming and OnMouseDown - still runs. 1.12's
// OnLayerMouseDown is the same function without the check, so the co-hook
// adds it: a disabled slider goes straight to the base. With the drag bit
// never set, the slider's mouse-move ignores the cursor as it does for a
// slider not pressed.

#include "Game.h"
#include "Offsets.h"
#include "frame/EnableScripts.h"

#include <cstdint>

namespace Slider::Enable {

namespace {

using MouseDown_t = void(__fastcall *)(void *frame, void *edx, void *event);

MouseDown_t g_origMouseDown = nullptr;

bool IsDisabled(void *slider) {
    return (Game::Read<uint32_t>(slider, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_DISABLED) != 0;
}

void __fastcall MouseDown_h(void *slider, void *edx, void *event) {
    if (IsDisabled(slider))
        reinterpret_cast<MouseDown_t>(Offsets::FUN_FRAME_ON_MOUSE_DOWN)(slider, edx, event);
    else
        g_origMouseDown(slider, edx, event);
}

const Game::HookAutoRegister _mouseDownHook{Offsets::FUN_SLIDER_ON_MOUSE_DOWN,
                                            reinterpret_cast<void *>(&MouseDown_h),
                                            reinterpret_cast<void **>(&g_origMouseDown)};

void *ResolveSlider(void *L) { return Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_SLIDER_LUA_TYPE_ID); }

int __fastcall Script_Enable(void *L) {
    if (void *slider = ResolveSlider(L))
        Frame::EnableScripts::RunOnEnableScript(slider, false);
    return 0;
}

int __fastcall Script_Disable(void *L) {
    if (void *slider = ResolveSlider(L))
        Frame::EnableScripts::RunOnEnableScript(slider, true);
    return 0;
}

int __fastcall Script_IsEnabled(void *L) {
    void *slider = ResolveSlider(L);
    if (slider == nullptr)
        return 0;
    Game::Lua::PushBool(L, !IsDisabled(slider));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"Enable", &Script_Enable},
    {"Disable", &Script_Disable},
    {"IsEnabled", &Script_IsEnabled},
};

const Game::Doc::Field kIsEnabledRets[] = {
    Game::Doc::Req("enabled", "boolean"),
};
const Game::Doc::Function kEnable{"Enables the slider and runs its OnEnable script.", {}, {}};
const Game::Doc::Function kDisable{
    "Disables the slider: the mouse no longer moves it. Runs its OnDisable script.", {}, {}};
const Game::Doc::Function kIsEnabled{"Returns whether the slider is enabled.", {}, kIsEnabledRets};

const Game::Doc::Method g_methodDocs[] = {
    {"Enable", &kEnable},
    {"Disable", &kDisable},
    {"IsEnabled", &kIsEnabled},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_SLIDER_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Slider::Enable
