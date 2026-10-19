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

#pragma once

#include <cstdint>

namespace Creature::Info {

// The client creature-cache record for a creature/NPC entry, or null when the
// creature is not cached (creaturecache.wdb, or queried this session). A
// synchronous peek — no network query. The record outlives the session's
// frames, which is why PlayerModel panes keep a pointer to it: it is what the
// engine's SetDisplay takes, and `PlayerModel:SetCreature` passes it there.
const uint8_t *CacheRecord(uint32_t creatureID);

// The faction group a player race belongs to, resolved the way
// `Script_UnitFactionGroup` does it for a player whose object isn't in the
// world (its name-cache branch, which reads the race): ChrRaces -> FactionTemplate
// group mask -> the first FactionGroup row whose bit is set and whose
// localized name is non-empty (skipping "Player" / "Monster"). On success
// `english` is the locale-independent tag ("Alliance") and `localized` the
// display name; both point into DBC string storage. False for an unknown or
// non-positive race, or a race with no named group. Backs
// `C_CreatureInfo.GetFactionInfo` and `UnitFactionGroup("player")` before the
// player object spawns.
bool FactionGroupForRace(int raceID, const char **english, const char **localized);

} // namespace Creature::Info
