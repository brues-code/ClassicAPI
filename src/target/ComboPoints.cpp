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

// GetComboPoints over a hidden (SoftTargetForce) target. 1.12's
// Script_GetComboPoints (0x0051A190) reports the points whenever the combo
// target is the raw selection (0x00B4E2D8); 3.80.2 resolves the `target`
// token, which is empty for a Force target, and answers 0. Without that, the
// ComboFrame (parented to UIParent, not TargetFrame) and combat text showed
// points for a target the UI does not show, and kept them across a silent
// Force swap. The points come back on the PLAYER_TARGET_CHANGED that a
// promotion fires.

#include "Game.h"
#include "Offsets.h"
#include "target/Reason.h"

namespace Target::ComboPoints {

namespace {

Game::Lua::CFunction g_original = nullptr;

int __fastcall GetComboPoints_h(void *L) {
    if (!Reason::Hidden())
        return g_original(L);
    Game::Lua::PushNumber(L, 0.0);
    return 1;
}

const Game::HookAutoRegister _hook{Offsets::FUN_SCRIPT_GET_COMBO_POINTS,
                                   reinterpret_cast<void *>(&GetComboPoints_h),
                                   reinterpret_cast<void **>(&g_original)};

} // namespace

} // namespace Target::ComboPoints
