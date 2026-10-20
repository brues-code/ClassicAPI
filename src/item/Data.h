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

namespace Item::Data {

// Warm the item-stats cache for `itemID`. If already cached this is a
// no-op (caller can read item info immediately). If not cached,
// triggers the engine's `SMSG_ITEM_QUERY_SINGLE_RESPONSE` round-trip
// and fires `GET_ITEM_INFO_RECEIVED` when the response arrives.
//
// Used by:
//   - Our `Script_GetItemInfo` hook (so 1.12's `GetItemInfo` becomes
//     transparent like 5.x+)
//   - `GameTooltip:SetItemByID` (so item tooltips warm the cache when
//     opened cold)
//
// Note: this is the **implicit** warmup path — fires GET_ITEM_INFO_RECEIVED
// only. For modern explicit-request semantics (fires
// ITEM_DATA_LOAD_RESULT), use `C_Item.RequestLoadItemData(ByID)` from
// Lua.
//
// Returns true when the item is cached or its load is queued, false when
// the queue is full and nothing will report on it.
bool WarmCache(uint32_t itemID);

// C++ observer of every queued load's completion: called with the itemID
// and the outcome each time this module fires GET_ITEM_INFO_RECEIVED or
// ITEM_DATA_LOAD_RESULT for it, after the cache holds the record (success)
// or the server refused it / every resend went unanswered (failure). Place
// a file-scope `static const Item::Data::LoadSubscriber`; order across
// subscribers is unspecified.
struct LoadSubscriber {
    using Callback = void (*)(uint32_t itemID, bool success);
    explicit LoadSubscriber(Callback cb);
    Callback cb;
    LoadSubscriber *next;
};

// Hook for `Script_GetItemInfo` (`0x0048E070`). Pre-warms the cache
// from arg 1 (number or `"item:NNN..."` string), then runs the
// original. The original still returns nil this call for cache
// misses, but the query is now in flight; subsequent calls return
// valid data and `GET_ITEM_INFO_RECEIVED` fires when ready.
using Script_GetItemInfo_t = int(__fastcall *)(void *L);
extern Script_GetItemInfo_t Script_GetItemInfo_o;
int __fastcall Script_GetItemInfo_h(void *L);

} // namespace Item::Data
