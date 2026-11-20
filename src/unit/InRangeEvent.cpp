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

// `UNIT_IN_RANGE_UPDATE: unitTarget, isInRange` — the first return of
// UnitInRange for a unit, as the 3.80.2 client polls it (0x141AE3480): its
// answer is Unit::Range::InRange, UnitInRange's own check, and the rest is
// Unit::PolledEvent's (RegisterUnitEvent units only, per-GUID answers, a
// first-check fire, one fire per token).
//
// Nothing in the client reacts to a distance: the player moves every frame
// while it moves, and the client moves each other unit in view a little
// every frame between its movement packets. So a poll is the mechanism, and
// only the units a frame named are checked, as retail's CompactUnitFrame
// expects ("C++ does extra work to send these events while any frame is
// registered for them").

#include "Game.h"
#include "event/Custom.h"
#include "tick/WorldTick.h"
#include "unit/PolledEvent.h"
#include "unit/Range.h"

#include <cstdint>

namespace Unit::InRangeEvent {

namespace {

constexpr const char *kName = "UNIT_IN_RANGE_UPDATE";

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("isInRange", "bool"),
};
const Game::Doc::Event kDoc{
    "Unit", "Fires when the unit moves into or out of the range that UnitInRange checks.",
    kPayload};
const Event::Custom::AutoReserve kEvent{kName, &kDoc};

bool IsInRange(uint64_t guid) {
    bool inRange = false;
    Unit::Range::InRange(guid, &inRange);
    return inRange;
}

Unit::PolledEvent::Poller g_poller{kName, kEvent, &IsInRange};

void OnWorldTick() { g_poller.Tick(); }

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Unit::InRangeEvent
