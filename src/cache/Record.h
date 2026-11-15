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

// The client query caches' `_GetRecord` (item, quest, creature, game object):
// one shape for every cache class, at a different address per class.
//
//   const uint8_t *__thiscall GetRecord(cache, id, const uint64_t *guid,
//                                       callback, userData, char dedup)
//
// It returns the record when the cache holds it, else nullptr. Only a
// non-null `callback` makes a miss create the pending entry and send the query
// (the branch at 0x0055BAAC tests the callback, not the last argument), and the
// engine later calls `callback(userData, success)` (`__stdcall`, `ret 8`). With
// `dedup`, a pending entry that already holds the same (callback, userData)
// pair is left alone (0x00562BC3..0x00562BF3); without it every call appends
// another node. `guid` points at 8 bytes the engine copies into a new pending
// entry.
namespace Cache::Record {

// One `_GetRecord` call. `getRecord` is the class's FUN_*_GET_RECORD and
// `cache` its VAR_*_CACHE instance.
const uint8_t *Get(uintptr_t getRecord, uintptr_t cache, uint32_t id, const uint64_t *guid,
                   void *callback, void *userData, bool dedup);

// A passive lookup: the record, or nullptr. Never sends a query.
const uint8_t *Peek(uintptr_t getRecord, uintptr_t cache, uint32_t id);

} // namespace Cache::Record
