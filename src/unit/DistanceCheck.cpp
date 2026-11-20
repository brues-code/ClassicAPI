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
// distance core (0x141BB0FD0, also UnitDistanceSquared's):
//   - Measured from the active mover, else the player (Unit::Range::FromUnit).
//   - 2D: x and y only, squared, rounded to a whole number.
//   - A unit in view gives its position; a member out of view the position
//     of its member record (SMSG_PARTY_MEMBER_STATS, Group::MemberStats).
//   - With no position, the distance stays 0, so the answer is true.
// Its gate (0x141698520) measures only you and group members, and only on
// open-world maps, so every other unit, pets and every instance are true.
// Here the gate is you and every group unit (Group::Roster::IsGroupUnit:
// members and their pets), on every map; any other unit is true. 3.80.2
// does not compare maps, so a member on another continent is measured across
// two coordinate spaces; here a member record on another map is false. The
// maps compare as the client names them, through AreaTable's map column for
// both areas (VAR_PLAYER_AREA_ID is what the record builder 0x005F0880 stores
// as the area).

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "object/Resolve.h"
#include "event/Custom.h"
#include "group/MemberStats.h"
#include "group/Roster.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"
#include "unit/PolledEvent.h"
#include "unit/Position.h"
#include "unit/Range.h"

#include <cmath>
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

// The map an area is on (AreaTable's map column), -1 for an unknown area.
int MapOfArea(uint32_t areaId) {
    const uint8_t *row = DBC::Record(Offsets::VAR_AREATABLE_RECORDS, Offsets::VAR_AREATABLE_COUNT, areaId);
    return row != nullptr ? Game::Read<int>(row, Offsets::OFF_AREATABLE_MAP_ID) : -1;
}

bool IsInDistance(uint64_t guid) {
    if (guid != Unit::Identity::PlayerGuid() && !Group::Roster::IsGroupUnit(guid))
        return true;
    float from[3] = {};
    if (!Unit::Position::Read(Unit::Range::FromUnit(), from))
        return true;

    float x = 0.0f;
    float y = 0.0f;
    float pos[3] = {};
    uint32_t area = 0;
    if (Unit::Position::Read(Object::ByGuid(Offsets::TYPEMASK_UNIT, guid), pos)) {
        x = pos[0];
        y = pos[1];
    } else if (Group::MemberStats::Position(guid, &x, &y, &area)) {
        const int map = MapOfArea(area);
        const int playerMap = MapOfArea(Game::Read<uint32_t>(Offsets::VAR_PLAYER_AREA_ID) & 0xFFFF);
        if (map != -1 && playerMap != -1 && map != playerMap)
            return false;
    } else {
        return true;
    }
    const float dx = x - from[0];
    const float dy = y - from[1];
    return std::llround(dx * dx + dy * dy) < kDistanceSquared;
}

Unit::PolledEvent::Poller g_poller{kName, kEvent, &IsInDistance};

void OnWorldTick() { g_poller.Tick(); }

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

} // namespace

} // namespace Unit::DistanceCheck
