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

// Re-sending a DBCache query.
//
// A DBCache `_GetRecord` sends its query only when it creates the pending
// node; a lookup that finds the node pending just adds its callback. The
// server drops a STATUS_LOGGEDIN query (every *_QUERY opcode) while the
// player is not in the world - before login completes and behind every
// loading screen - so a query sent then is never answered, and the node
// stays pending for the rest of the session. Sending the query again is the
// only way out; the answer then fills the node and runs every callback on it
// through the engine's own receive.

namespace Cache::Query {

// Sends `cache`'s query for `key` exactly as its `_GetRecord` does for a new
// node: the cache's opcode, the key, and a zero GUID when the cache's owner
// flag is set.
void Send(void *cache, uint32_t key);

} // namespace Cache::Query
