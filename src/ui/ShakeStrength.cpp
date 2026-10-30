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

// The `ShakeStrengthUI` cvar. Nothing in the engine reads it: the embedded
// addon's ScriptAnimationUtil.ShakeFrame scales each shake offset by it and
// skips the shake at 0 or less. Help, category and default are retail's.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"

namespace UI::ShakeStrength {

namespace {

void Register() {
    CVar::Factory::Register("ShakeStrengthUI",
                            "Motion sickness control for how much effects can shake in 2D UI",
                            Offsets::CVAR_CATEGORY_GAME, "1.000000");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace UI::ShakeStrength
