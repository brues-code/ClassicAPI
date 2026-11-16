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

#include "cache/Record.h"

#include "Offsets.h"

namespace Cache::Record {

const uint8_t *Get(uintptr_t getRecord, uintptr_t cache, uint32_t id, const uint64_t *guid,
                   void *callback, void *userData, bool dedup) {
    using GetRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t id,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, char dedup);
    return reinterpret_cast<GetRecord_t>(getRecord)(reinterpret_cast<void *>(cache), id, guid,
                                                    callback, userData, dedup ? 1 : 0);
}

const uint8_t *Peek(uintptr_t getRecord, uintptr_t cache, uint32_t id) {
    const uint64_t none = 0;
    return Get(getRecord, cache, id, &none, nullptr, nullptr, false);
}

const uint8_t *GetPlayer(uint64_t guid, const uint64_t *owner, void *callback, void *userData,
                         bool dedup) {
    using GetRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t lo, uint32_t hi,
                                                      const uint64_t *owner, void *callback,
                                                      void *userData, char dedup);
    return reinterpret_cast<GetRecord_t>(Offsets::FUN_PLAYER_INFO_LOOKUP)(
        reinterpret_cast<void *>(Offsets::VAR_PLAYER_NAME_CACHE), static_cast<uint32_t>(guid),
        static_cast<uint32_t>(guid >> 32), owner, callback, userData, dedup ? 1 : 0);
}

} // namespace Cache::Record
