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

// `editBox:Enable()`, `editBox:Disable()` and `editBox:IsEnabled()`.
//
// 4.3.4's CSimpleEditBox::Enable(enabled): a disable first clears the box's
// keyboard focus, then either runs CSimpleFrame::RunOnEnableScript - frame
// flag 0x400 and the OnEnable / OnDisable script - on every call, changed or
// not. IsEnabled reads the flag. That is all a disable does in 4.3.4: the
// mouse-down and SetFocus paths never read the flag, so a click still
// focuses a disabled box.

#include "Game.h"
#include "Offsets.h"
#include "frame/EnableScripts.h"

#include <cstdint>

namespace EditBox::Enable {

namespace {

using ClearFocus_t = void(__fastcall *)(void *editBox);

void Enable(void *editBox, bool enabled) {
    if (!enabled)
        reinterpret_cast<ClearFocus_t>(Offsets::FUN_EDITBOX_CLEAR_FOCUS)(editBox);
    Frame::EnableScripts::RunOnEnableScript(editBox, !enabled);
}

int __fastcall Script_Enable(void *L) {
    void *editBox = Game::Lua::ResolveEditBox(L);
    if (editBox != nullptr)
        Enable(editBox, true);
    return 0;
}

int __fastcall Script_Disable(void *L) {
    void *editBox = Game::Lua::ResolveEditBox(L);
    if (editBox != nullptr)
        Enable(editBox, false);
    return 0;
}

int __fastcall Script_IsEnabled(void *L) {
    void *editBox = Game::Lua::ResolveEditBox(L);
    if (editBox == nullptr)
        return 0;
    const uint32_t flags = Game::Read<uint32_t>(editBox, Offsets::OFF_FRAME_FLAGS);
    Game::Lua::PushBool(L, (flags & Offsets::FRAME_FLAG_DISABLED) == 0);
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
const Game::Doc::Function kEnable{"Enables the edit box and runs its OnEnable script.", {}, {}};
const Game::Doc::Function kDisable{
    "Disables the edit box: clears its keyboard focus and runs its OnDisable script.", {}, {}};
const Game::Doc::Function kIsEnabled{"Returns whether the edit box is enabled.", {}, kIsEnabledRets};

const Game::Doc::Method g_methodDocs[] = {
    {"Enable", &kEnable},
    {"Disable", &kDisable},
    {"IsEnabled", &kIsEnabled},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_EDITBOX_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace EditBox::Enable
