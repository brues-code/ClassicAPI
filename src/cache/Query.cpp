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

namespace Cache::Query {

namespace {

// The engine's stack CDataStore (see Offsets::VTABLE_CDATASTORE).
struct DataStore {
    uintptr_t vtable;
    void *buffer;
    uint32_t base;
    uint32_t alloc;
    uint32_t size;
    uint32_t read;
};
static_assert(sizeof(DataStore) == 0x18, "CDataStore layout");

using PutU32_t = void(__thiscall *)(DataStore *self, uint32_t value);
using PutGuid_t = void(__thiscall *)(DataStore *self, uint32_t lo, uint32_t hi);
using Send_t = void(__thiscall *)(DataStore *self);
using Free_t = void(__thiscall *)(DataStore *self, void **buffer, uint32_t *base, uint32_t *alloc);

} // namespace

// The send of FUN_DBCACHE_ITEMSTATS_GET_RECORD (0x0055BB18..0x0055BB89),
// step for step. Every DBCache<T> clone builds the same packet.
void Send(void *cache, uint32_t key) {
    DataStore packet{Offsets::VTABLE_CDATASTORE, nullptr, 0, 0, 0, 0xFFFFFFFF};
    const auto putU32 = reinterpret_cast<PutU32_t>(Offsets::FUN_DATASTORE_PUT_U32);
    putU32(&packet, Game::Read<uint32_t>(cache, Offsets::OFF_DBCACHE_QUERY_OPCODE));
    putU32(&packet, key);
    if (Game::Read<uint8_t>(cache, Offsets::OFF_DBCACHE_OWNER_FLAG) != 0)
        reinterpret_cast<PutGuid_t>(Offsets::FUN_DATASTORE_PUT_GUID)(&packet, 0, 0);
    packet.read = 0;
    reinterpret_cast<Send_t>(Offsets::FUN_CLIENT_SERVICES_SEND)(&packet);
    if (packet.alloc != 0xFFFFFFFF) {
        const auto release =
            *reinterpret_cast<const Free_t *>(Offsets::VTABLE_CDATASTORE + sizeof(uintptr_t));
        release(&packet, &packet.buffer, &packet.base, &packet.alloc);
    }
}

} // namespace Cache::Query
