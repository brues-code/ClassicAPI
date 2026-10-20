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

// `button:SetEnabled(enabled)` — Enable() or Disable() by a bool. 1.12's
// Script Enable / Disable (0x0077FEF0 / 0x0077FFD0) resolve the button and
// call its Enable vmethod with 1 / 0; this does the same with the truth of
// the argument, so CheckButton's override and the OnEnable / OnDisable
// scripts (Frame::EnableScripts) run as they do for the stock methods.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::SetEnabled {

namespace {

using ButtonEnable_t = void(__thiscall *)(void *button, int enable);

int __fastcall Script_SetEnabled(void *L) {
    void *button = Game::Lua::ResolveTypedObject(L, 1, Offsets::VAR_BUTTON_LUA_TYPE_ID);
    if (button == nullptr)
        return 0;
    const int enable = Game::Lua::ToBoolean(L, 2) != 0 ? 1 : 0;
    auto *vtable = Game::Read<uintptr_t *>(button, 0);
    reinterpret_cast<ButtonEnable_t>(vtable[Offsets::VTBL_BUTTON_ENABLE / sizeof(uintptr_t)])(button, enable);
    return 0;
}

const Game::Lua::FrameMethodEntry kMethods[] = {
    {"SetEnabled", &Script_SetEnabled},
};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Opt("enabled", "boolean", "false", "True enables the button, false disables it."),
};
const Game::Doc::Function kDoc{"Enables or disables the button, as Enable() or Disable().", kArgs, {}};
const Game::Doc::Method kDocs[] = {{"SetEnabled", &kDoc}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_BUTTON_METHOD_REGISTRY), kMethods, 1,
                                    kDocs, 1);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::SetEnabled
