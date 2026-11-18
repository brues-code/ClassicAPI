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

// PLAYER_LEVEL_CHANGED(oldLevel, newLevel, real): retail's event for a change
// of the player's UNIT_FIELD_LEVEL.
//
// 1.12 broadcasts UNIT_LEVEL for the player when that field changes: the
// engine's field observer (FUN_0051bd50) calls FUN_UNIT_EVENT_BROADCAST with
// the field index as the event id (UNIT_EVENT_UNIT_LEVEL), and every call of
// it goes through Unit::TokenEvents::Broadcast. The event rides on that, for
// the player's GUID, with the level the player had before: kept from world
// entry and from each change. PLAYER_LEVEL_UP, the level-up message, comes
// before the field changes, so it fires first, as in retail.
//
// `real` is retail's false for a level that only scales (level sync, a
// timewalking level); 1.12 has no such level, so it is always true.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/EnteringWorld.h"
#include "unit/Identity.h"
#include "unit/TokenEvents.h"

#include <cstdint>

namespace Unit::LevelChanged {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("oldLevel", "number"),
    Game::Doc::Req("newLevel", "number"),
    Game::Doc::Req("real", "bool", "Always true on this client."),
};
const Game::Doc::Event kDoc{"Unit", "Fires when the player's level changes.", kPayload};
const Event::Custom::AutoReserve kEvent{"PLAYER_LEVEL_CHANGED", &kDoc};

uint32_t g_level = 0; // the player's level as last seen; 0 before world entry

uint32_t PlayerLevel() {
    const uint8_t *fields = Unit::Identity::PlayerDescriptor();
    return fields != nullptr ? Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_LEVEL) : 0;
}

void OnEnterWorld() { g_level = PlayerLevel(); }

const Event::EnteringWorld::AutoObserve _enterWorld{&OnEnterWorld};

void OnUnitEvent(uint64_t guid, uint32_t eventId) {
    if (eventId != Offsets::UNIT_EVENT_UNIT_LEVEL || guid == 0 ||
        guid != Unit::Identity::PlayerGuid())
        return;
    const uint32_t level = PlayerLevel();
    const uint32_t old = g_level;
    g_level = level;
    if (old == 0 || level == 0 || level == old)
        return;
    const int event = kEvent.Slot();
    void *L = Game::Lua::State();
    if (!Event::Custom::HasListeners(event) || L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushNumber(L, static_cast<double>(old));
    Game::Lua::PushNumber(L, static_cast<double>(level));
    Game::Lua::PushBool(L, true);
    Event::Custom::FireValues(event, L, top + 1, 3);
    Game::Lua::SetTop(L, top);
}

const Unit::TokenEvents::AutoObserve _unitEvent{&OnUnitEvent};

} // namespace

} // namespace Unit::LevelChanged
