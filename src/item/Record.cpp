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

#include "Record.h"

#include "Offsets.h"
#include "cache/Record.h"

namespace Item {

const uint8_t *PeekRecord(uint32_t itemID) {
    return Cache::Record::Peek(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD,
                               Offsets::VAR_ITEMDB_CACHE, itemID);
}

} // namespace Item
