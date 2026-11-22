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

// `boss1`..`boss5` unit tokens and INSTANCE_ENCOUNTER_ENGAGE_UNIT.
//
// The 3.80.2 client keeps a list of ENCOUNTER_UNIT nodes (sentinel
// 0x1444CBF10, count 0x1444CBF20), each a GUID and a priority byte:
//   - Engage 0x141722520(guid, priority) adds the unit, or updates the
//     priority of one already listed, and sorts the list by priority
//     (0x141724BC0). Disengage 0x1417226C0 removes it. Both fire
//     INSTANCE_ENCOUNTER_ENGAGE_UNIT (hash 0x9B19400D97806B12), which has no
//     payload, once per call.
//   - The server drives both: its engage / disengage unit messages (handlers
//     0x1417213A0 / 0x1417213B0), and aura 357 (0x165), whose apply engages
//     its unit with the aura's misc value as the priority (0x1414984A0) and
//     whose removal disengages it.
//   - `bossN` is the Nth unit of the list (0x141723EB0, index N-1); readers
//     cap the walk at five (0x141B8FE70, 0x141B90E10).
//
// 1.12's server sends neither, so the list here is the bosses of the
// encounter CombatLog::Encounter tracks: the world bosses (rank 3) and the
// creatures addons list (CombatLog::BossCreatures) that the group fights, in
// the order they joined the fight. A boss stays on the list, dead
// or not, until the encounter ends (kill, wipe, loading screen), as a 3.80.2
// server disengages its bosses when the encounter is over. There is no
// priority, so engage order is the order. Unlisted bosses of rank below 3
// (most dungeon bosses) and the adds a server would mark never get a token.
//
// Resolution is the `bossN` branch of the token resolver
// (unit/TokenExtensions.cpp), so every `Unit*` takes the tokens. Unit events
// reach `bossN` through Unit::TokenEvents, as they reach `markN`.

#include "unit/Boss.h"

#include "Game.h"
#include "combatlog/Encounter.h"
#include "event/Custom.h"
#include "unit/TokenEvents.h"

#include <cstdint>

namespace Unit::Boss {

namespace {

const Game::Doc::Event kEngageUnitDoc{
    "InstanceEncounter",
    "Fires when a unit joins or leaves the boss1 to boss5 unit tokens. A boss joins at "
    "its first combat log entry with your group, and every boss leaves when the encounter "
    "ends.",
    {}};
const Event::Custom::AutoReserve _reserve{"INSTANCE_ENCOUNTER_ENGAGE_UNIT", &kEngageUnitDoc};

} // namespace

uint64_t GetGUIDByIndex(int index) {
    if (index < 1 || index > kTokenCount)
        return 0;
    return CombatLog::Encounter::EngagedBoss(index - 1);
}

int IndexOf(uint64_t guid) {
    if (guid == 0)
        return 0;
    for (int i = 1; i <= kTokenCount; ++i)
        if (GetGUIDByIndex(i) == guid)
            return i;
    return 0;
}

void Changed(uint64_t guid) {
    TokenEvents::Refresh(guid);
    Event::Custom::Fire(_reserve.Slot(), "");
}

} // namespace Unit::Boss
