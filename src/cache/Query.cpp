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

#include "cache/Query.h"

#include "Game.h"
#include "Offsets.h"
#include "net/PacketWriter.h"

namespace Cache::Query {

// The send of FUN_DBCACHE_ITEMSTATS_GET_RECORD (0x0055BB18..0x0055BB89),
// step for step. Every DBCache<T> clone builds the same packet.
void Send(void *cache, uint32_t key) {
    Net::OutPacket packet(Game::Read<uint32_t>(cache, Offsets::OFF_DBCACHE_QUERY_OPCODE));
    packet.PutU32(key);
    if (Game::Read<uint8_t>(cache, Offsets::OFF_DBCACHE_OWNER_FLAG) != 0)
        packet.PutGuid(0);
    packet.Send();
}

} // namespace Cache::Query
