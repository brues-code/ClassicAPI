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

// `C_Macro.SetMacroDisplay(macroSlot, value)` — the Lua door onto
// `Macro::ShowTooltip::Publish`, for an addon that has its own macro parser
// and wants the display done properly rather than by replacing the action
// globals. See the ShowTooltip header for what publishing buys.
//
// Three states, which is all a publisher needs:
//
//   SetMacroDisplay(1, "Frostbolt")   -- resolve and show this
//   SetMacroDisplay(1, false)         -- mine, nothing matched, show `?`
//   SetMacroDisplay(1, nil)           -- released, our own parse resumes
//
// The value takes exactly the forms a `#showtooltip` line takes, so a
// publisher hands over what it would have written in the macro and gets the
// item-before-spell precedence, `item:N`, links, inventory slots and
// `bag slot` for free.

#include "Game.h"
#include "macro/ShowTooltip.h"

namespace Macro::Display {

namespace {

int __fastcall Script_C_Macro_SetMacroDisplay(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Macro.SetMacroDisplay(macroSlot, value)");
        return 0;
    }
    const int macroSlot = static_cast<int>(Game::Lua::ToNumber(L, 1));

    // Released when the value is absent or nil; claimed-but-unresolved for a
    // false or an empty string. `lua_type` reports NONE past the top of the
    // stack, so test the top rather than the type alone.
    const bool released = Game::Lua::GetTop(L) < 2 ||
                          Game::Lua::Type(L, 2) == Game::Lua::TYPE_NIL;
    if (released) {
        Macro::ShowTooltip::Release(macroSlot);
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    const char *value = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    Game::Lua::PushBoolean(L, Macro::ShowTooltip::Publish(macroSlot, value) ? 1 : 0);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Macro", "SetMacroDisplay",
                                     &Script_C_Macro_SetMacroDisplay);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Display
