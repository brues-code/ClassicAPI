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

// The `ActionButtonUseKeyDown` cvar — registered by 4.3.4's engine
// (GameUI_CVar.cpp, "Activate the action button on a keydown") and read only
// by FrameXML's action-button key handlers, which this project's addon ports
// (Util/ActionButton.lua). The engine half is the registration. The default
// is 0, keeping 1.12's action buttons firing on key release; 4.3.4 shipped 1.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"

namespace Action::UseKeyDown {

namespace {

void Register() {
    CVar::Factory::Register("ActionButtonUseKeyDown", "Activate the action button on a keydown",
                            Offsets::CVAR_CATEGORY_GAME, "0");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Action::UseKeyDown
