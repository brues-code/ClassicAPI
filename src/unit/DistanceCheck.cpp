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

// `UNIT_DISTANCE_CHECK_UPDATE: unitTarget, isInDistance` — whether a unit is
// within 250 yards, as the 3.80.2 client polls it (0x141AE33A0), with the
// rest Unit::PolledEvent's (RegisterUnitEvent units only, per-GUID answers, a
// first-check fire, one fire per token). Retail's one reader,
// CompactUnitFrame_GetCenterStatusIconState, shows the other-phase icon only
// for a unit in distance.
//
// 3.80.2's answer is `distanceSquared < 62500` (0x146AD78A0 + 0x48), from its
// distance core (0x141BB0FD0, also UnitDistanceSquared's: Unit::Range::
// DistanceSquared). With no position, the distance stays 0, so the answer is
// true. Its gate (0x141698520) measures only you and group members, and only
// on open-world maps, so every other unit, pets and every instance are true.
// Here the gate is you and every group unit (Group::Roster::IsGroupUnit:
// members and their pets), on every map; any other unit is true. A member
// record on another map is false (3.80.2 measures it across two coordinate
// spaces).

#include "event/Custom.h"
#include "group/Roster.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"
#include "unit/PolledEvent.h"
#include "unit/Range.h"

#include <cstdint>

namespace Unit::DistanceCheck {

namespace {

constexpr const char *kName = "UNIT_DISTANCE_CHECK_UPDATE";
constexpr long long kDistanceSquared = 62500; // 250 yards

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken"),
    Game::Doc::Req("isInDistance", "bool"),
};
const Game::Doc::Event kDoc{"Unit", "Fires when a unit comes within 250 yards of you or leaves that distance.",
                            kPayload};
const Event::Custom::AutoReserve kEvent{kName, &kDoc};

bool IsInDistance(uint64_t guid) {
    if (guid != Unit::Identity::PlayerGuid() && !Group::Roster::IsGroupUnit(guid))
        return true;
    long long squared = 0;
    const Unit::Range::Distance found = Unit::Range::DistanceSquared(guid, &squared);
    return found != Unit::Range::Distance::OtherMap && squared < kDistanceSquared;
}

Unit::PolledEvent::Poller g_poller{kName, kEvent, &IsInDistance};

void OnWorldTick() { g_poller.Tick(); }

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Unit::DistanceCheck
