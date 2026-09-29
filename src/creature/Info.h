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

// The cached model display ID for a creature/NPC entry, or 0 when the creature
// is not in the client creature cache (creaturecache.wdb, or queried this
// session). A synchronous peek — no network query. Backs `Model:SetCreature`:
// the cache being keyed by entry ID and carrying a display ID is exactly the
// entry-ID → model mapping vanilla has no DBC for.
uint32_t DisplayID(uint32_t creatureID);

// The faction group a player race belongs to, resolved the way the player
// branch of `Script_UnitFactionGroup` does it: ChrRaces -> FactionTemplate
// group mask -> the first FactionGroup row whose bit is set and whose
// localized name is non-empty (skipping "Player" / "Monster"). On success
// `english` is the locale-independent tag ("Alliance") and `localized` the
// display name; both point into DBC string storage. False for an unknown or
// non-positive race, or a race with no named group. Backs
// `C_CreatureInfo.GetFactionInfo` and `UnitFactionGroup("player")` before the
// player object spawns.
bool FactionGroupForRace(int raceID, const char **english, const char **localized);

} // namespace Creature::Info
