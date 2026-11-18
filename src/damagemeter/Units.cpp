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

#include "damagemeter/Units.h"

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "dbc/Names.h"
#include "guid/Guid.h"

namespace DamageMeter {

uint64_t CreatureKey(uint32_t creatureID) {
    return (static_cast<uint64_t>(Guid::PREFIX_CREATURE) << 48) | creatureID;
}

const char *Classification(uint32_t creatureID) {
    uint32_t rank = 0;
    if (creatureID != 0) {
        if (const uint8_t *rec = Creature::Info::CacheRecord(creatureID))
            rank = Game::Read<uint32_t>(rec, Offsets::OFF_CREATURE_RANK);
    }
    if (rank >= Offsets::UNIT_CLASSIFICATION_COUNT)
        rank = 0;
    return Game::Read<const char *>(Offsets::VAR_UNIT_CLASSIFICATION_NAMES + rank * 4);
}

void SetPlayerDetails(MeterUnit &unit, uint32_t classID, uint32_t raceID) {
    if (const char *token = DBC::ClassToken(classID))
        unit.classFilename = token;
    const char *english = nullptr;
    const char *localized = nullptr;
    if (Creature::Info::FactionGroupForRace(static_cast<int>(raceID), &english, &localized))
        unit.factionGroup = english;
}

} // namespace DamageMeter
