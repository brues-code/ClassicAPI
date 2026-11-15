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

#include "Cache.h"

#include "Offsets.h"
#include "cache/Record.h"
#include "unit/Identity.h"

namespace Quest::Cache {

// The quest cache's `_GetRecord` (Cache::Record). The 8-byte zero block
// mirrors the engine's own call in `Script_GetQuestLogQuestText` at
// `0x004DFF20`. `unique` is the dedup byte: the pending entry's callback list
// is at `entry+0x1904`, with the callback at node `+0x08` and userData at
// `+0x18`.
const uint8_t *Lookup(uint32_t questID, void *callback, void *userData, bool unique) {
    const uint64_t outBuf = 0;
    return ::Cache::Record::Get(Offsets::FUN_DBCACHE_QUEST_GET_RECORD, Offsets::VAR_QUEST_CACHE,
                                questID, &outBuf, callback, userData, unique);
}

using Substitute_t = void(__fastcall *)(const char *src, char *dst, uint32_t size,
                                        const uint64_t *playerGuid);

void ExpandText(const char *src, char *dst, uint32_t size) {
    const uint64_t guid = Unit::Identity::PlayerGuid();
    reinterpret_cast<Substitute_t>(Offsets::FUN_QUEST_TEXT_SUBSTITUTE)(src, dst, size, &guid);
}

} // namespace Quest::Cache
