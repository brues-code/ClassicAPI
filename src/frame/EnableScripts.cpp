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

// The OnEnable and OnDisable scripts.
//
// 4.3.4 gives every frame the two scripts and one runner,
// CSimpleFrame::RunOnEnableScript(disabled): set or clear frame flag 0x400,
// then run OnDisable or OnEnable. Two classes call it: CSimpleButton::Enable,
// after the state change and only when the state changed, and
// CSimpleEditBox::Enable (EditBox::Enable, a separate module) on every call.
// 1.12 has neither script. The scripts are Frame::ExtraScripts scripts; the
// Button side co-hooks 1.12's CSimpleButton::Enable, which returns early in
// the same two cases 4.3.4's does, so the runner runs exactly when the
// button's state changed.
//
// 4.3.4 also skips the script while the frame is loading from XML. 1.12's
// Button constructor enables the button before any script is loaded, so no
// handler can exist yet at that point.

#include "frame/EnableScripts.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/ExtraScripts.h"
#include "frame/ScriptArgs.h"

#include <cstdint>

namespace Frame::EnableScripts {

namespace {

const Frame::ExtraScripts::Script g_onEnable{"onenable", Offsets::VAR_FRAME_LUA_TYPE_ID};
const Frame::ExtraScripts::Script g_onDisable{"ondisable", Offsets::VAR_FRAME_LUA_TYPE_ID};

using SetFrameFlag_t = void(__fastcall *)(void *frame, void *edx, uint32_t mask, int on);
using ButtonEnable_t = void(__fastcall *)(void *button, void *edx, int enable);

ButtonEnable_t g_origButtonEnable = nullptr;

void __fastcall ButtonEnable_h(void *button, void *edx, int enable) {
    const bool wasEnabled = Game::Read<int>(button, Offsets::OFF_BUTTON_STATE) != Offsets::BUTTON_STATE_DISABLED;
    g_origButtonEnable(button, edx, enable);
    const bool isEnabled = Game::Read<int>(button, Offsets::OFF_BUTTON_STATE) != Offsets::BUTTON_STATE_DISABLED;
    if (isEnabled != wasEnabled)
        RunOnEnableScript(button, !isEnabled);
}

const Game::HookAutoRegister _buttonEnableHook{Offsets::FUN_BUTTON_ENABLE,
                                               reinterpret_cast<void *>(&ButtonEnable_h),
                                               reinterpret_cast<void **>(&g_origButtonEnable)};

} // namespace

void RunOnEnableScript(void *frame, bool disabled) {
    reinterpret_cast<SetFrameFlag_t>(Offsets::FUN_FRAME_SET_FRAME_FLAG)(frame, nullptr,
                                                                       Offsets::FRAME_FLAG_DISABLED, disabled);
    const uint32_t *slot = (disabled ? g_onDisable : g_onEnable).Slot(frame);
    if (slot == nullptr || slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    Frame::ScriptArgs::RunSlotWithValues(L, slot, frame, Game::Lua::GetTop(L) + 1, 0);
}

} // namespace Frame::EnableScripts
