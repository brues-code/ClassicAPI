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


// `Enum.InputContext` — retail's InputConstants enumeration: the input an
// edit box's OnCursorChanged names as its `context`. ScrollingEditBox
// (Blizzard_SharedXML) compares it with Enum.InputContext.Keyboard.

#include "Game.h"

namespace EditBox::InputContext {

namespace {

const Game::Lua::EnumIntegerEntry kEntries[] = {
    {"None", 0},
    {"Keyboard", 1},
    {"Mouse", 2},
    {"GamePad", 3},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "InputContext", kEntries,
                                   sizeof(kEntries) / sizeof(kEntries[0]), "InputConstants");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace EditBox::InputContext
