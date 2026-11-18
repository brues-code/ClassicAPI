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

#include "replay/Names.h"

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "player/Info.h"

#include <cstdlib>
#include <string_view>

namespace Replay {

// The unit's name: the log's, else the client's for its GUID (a player's from
// the name caches, a creature's from the creature cache). A unit only the
// movement file shows has none in the log. A lookup only: the viewer asks
// for an uncached creature (C_CreatureInfo.RequestLoadCreatureByID).
std::string UnitName(const Unit &unit) {
    if (!unit.name.empty())
        return unit.name;
    if (!unit.cachedName.empty())
        return unit.cachedName;
    if (unit.player) {
        constexpr std::string_view kPrefix = "Player-0-";
        if (unit.guid.rfind(kPrefix, 0) != 0)
            return {};
        const uint64_t guid = std::strtoull(unit.guid.c_str() + kPrefix.size(), nullptr, 16);
        char name[64];
        if (guid != 0 && Player::Info::NameFromGuid(guid, name, sizeof name))
            return name;
    } else if (unit.creatureID != 0) {
        const uint8_t *record = Creature::Info::CacheRecord(unit.creatureID);
        const char *name =
            record != nullptr ? Game::Read<const char *>(record, Offsets::OFF_CREATURE_NAME) : nullptr;
        if (name != nullptr)
            return name;
    }
    return {};
}

} // namespace Replay
