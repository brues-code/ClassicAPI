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

#include "map/Instance.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>

namespace Map::Instance {

int Type(int mapID) {
    const uint8_t *rec =
        DBC::Record(Offsets::VAR_MAP_RECORDS, Offsets::VAR_MAP_COUNT, static_cast<uint32_t>(mapID));
    return rec != nullptr ? Game::Read<int>(rec, Offsets::OFF_MAP_INSTANCE_TYPE) : -1;
}

int CurrentType() { return Type(Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID)); }

} // namespace Map::Instance
