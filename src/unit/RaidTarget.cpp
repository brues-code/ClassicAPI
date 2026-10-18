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

// `markN` raid-target-marker unit tokens (`mark1`=star … `mark8`=skull).
//
// Two halves, mirroring `Unit::Focus`:
//
//   1. Resolution — `GetGUIDByMark` reads the engine's marker table
//      (`VAR_RAID_TARGET_GUIDS`); the `markN` branch of the token
//      resolver in `unit/TokenExtensions.cpp` calls it so every stock
//      `Script_Unit*` (UnitHealth, UnitExists, UnitName, …) accepts
//      `"markN"` for free.
//
//   2. Events — every unit event the engine fires for a marked unit also
//      fires with `arg1 == "markN"` (`unit/TokenEvents.cpp`, which reads
//      `GetGUIDByMark`). The server writes the marker table
//      (`SMSG_RAID_TARGET_UPDATE`) with no event of its own, so a WorldTick
//      diff of the 8 slots tells TokenEvents which units gained or lost a
//      mark. A marked unit coming into range needs nothing here: the engine
//      evaluates it as it arrives. The idle case (no markers set) costs 8
//      compares.
//
// A marker on a non-unit GUID (totem corpse / gameobject / loot) never
// resolves to a `CGUnit`, so it fires no events — correct, and matches
// `UnitExists("markN")` being false there.

#include "unit/RaidTarget.h"

#include "Game.h"
#include "Offsets.h"
#include "tick/WorldTick.h"
#include "unit/TokenEvents.h"

#include <cstdint>

namespace Unit::RaidTarget {

namespace {

// The marker table as of the last tick, to see which slots changed.
uint64_t g_markGUID[kMarkCount] = {};

const uint64_t *MarkerTable() {
    return reinterpret_cast<const uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_RAID_TARGET_GUIDS));
}

void OnWorldTick() {
    const uint64_t *table = MarkerTable();
    for (int i = 0; i < kMarkCount; ++i) {
        const uint64_t now = table[i];
        if (now == g_markGUID[i])
            continue;
        const uint64_t old = g_markGUID[i];
        g_markGUID[i] = now;
        TokenEvents::Refresh(old);
        TokenEvents::Refresh(now);
    }
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// Reset the mirror on logout→character-select: the DLL isn't reloaded across
// a relog, and every unit object went with the world. Glue hook so it fires
// on logout but NOT on /reload.
void ClearOnLogout() {
    for (int i = 0; i < kMarkCount; ++i)
        g_markGUID[i] = 0;
}
const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

} // namespace

uint64_t GetGUIDByMark(int mark) {
    if (mark < 1 || mark > kMarkCount)
        return 0;
    return MarkerTable()[mark - 1];
}

} // namespace Unit::RaidTarget
