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

// PLAYER_IN_COMBAT_CHANGED(inCombat): retail's event for a change of
// UnitAffectingCombat("player").
//
// 1.12 has the same moment already: CGUnit_C::OnActivePlayerFlagsChanged
// (0x005FF527) signals PLAYER_REGEN_DISABLED when the player's
// UNIT_FLAG_IN_COMBAT bit (the bit UnitAffectingCombat reads) sets, and
// PLAYER_REGEN_ENABLED when it clears. The event rides on those two signals,
// with a real boolean, as retail's payload is.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"

namespace Unit::InCombatChanged {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("inCombat", "bool"),
};
const Game::Doc::Event kDoc{"PlayerScript",
                            "Fires when the player enters or leaves combat.", kPayload};
const Event::Custom::AutoReserve kEvent{"PLAYER_IN_COMBAT_CHANGED", &kDoc};

void Fire(bool inCombat) {
    const int event = kEvent.Slot();
    void *L = Game::Lua::State();
    if (!Event::Custom::HasListeners(event) || L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushBool(L, inCombat);
    Event::Custom::FireValues(event, L, top + 1, 1);
    Game::Lua::SetTop(L, top);
}

void OnRegenDisabled() { Fire(true); }
void OnRegenEnabled() { Fire(false); }

const Event::SignalHook::AutoObserve _disabled{Offsets::EVENT_PLAYER_REGEN_DISABLED,
                                               &OnRegenDisabled};
const Event::SignalHook::AutoObserve _enabled{Offsets::EVENT_PLAYER_REGEN_ENABLED,
                                              &OnRegenEnabled};

} // namespace

} // namespace Unit::InCombatChanged
