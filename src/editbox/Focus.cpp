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


// `GetCurrentKeyBoardFocus()` - the edit box that holds keyboard focus, or
// nil. 4.3.4 reads CSimpleEditBox::s_currentFocus and pushes its Lua object,
// registering it first if Lua has never seen it; Game::Lua::PushObject does
// the same register-on-demand.

#include "Game.h"
#include "Offsets.h"

namespace EditBox::Focus {

namespace {

int __fastcall Script_GetCurrentKeyBoardFocus(void *L) {
    void *focus = Game::Read<void *>(Offsets::VAR_FOCUSED_EDITBOX);
    if (focus == nullptr)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushObject(L, focus);
    return 1;
}

const Game::Doc::Field kRets[] = {Game::Doc::Opt("frame", "EditBox")};

const Game::Doc::Function kDoc{"Returns the edit box that has keyboard focus, or nil when none has it.", {}, kRets,
                               "FrameGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCurrentKeyBoardFocus", &Script_GetCurrentKeyBoardFocus, &kDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace EditBox::Focus
