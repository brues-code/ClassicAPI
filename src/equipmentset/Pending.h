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

#include "Set.h"

#include <cstdint>

// The swap in flight — 4.3.4's `m_swapPending` / `m_pendingSetID`.
//
// 4.3.4 sends one CMSG_EQUIPMENT_SET_USE and waits for
// SMSG_USE_EQUIPMENT_SET_RESULT. 1.12 has neither: a set swap is a series of
// ordinary CMSG_SWAP_ITEMs, and only a refused one answers
// (SMSG_INVENTORY_CHANGE_FAILURE). So the result is read off the engine:
//   success — the paperdoll holds what the swap asked for;
//   failure — the server refused a move that touched one of our items, or
//             the swap found no room for an item it had to take off;
//   neither — nothing more will arrive, because a move can wait on the
//             player (the engine parks a bind-on-equip item behind
//             EQUIP_BIND_CONFIRM, and a declined prompt sends nothing) or on
//             an item-cache query. The swap then ends as a failure once the
//             paperdoll has stopped changing for kSettleMs.
namespace EquipmentSet::Pending {

// 4.3.4 `CanUseEquipmentSets`: false while a swap is in flight.
bool CanUse();

// What one swap asked for. `want[i]` is what slot i+1 must end up holding
// (0 = empty); `cares[i]` is false for slots the swap left alone (ignored,
// combat-locked, missing items, or a move that could not be sent). `moved`
// lists every item GUID a move touched — set items and the items they
// displaced — so a refusal can be matched to this swap.
struct Request {
    uint32_t setID;
    uint64_t want[SLOT_COUNT];
    bool cares[SLOT_COUNT];
    uint64_t moved[SLOT_COUNT * 2];
    int movedCount;
    bool bagsFull; // an item to take off had no free bag slot
};

// Starts tracking `req`, fires EQUIPMENT_SWAP_PENDING and greys the set
// buttons. Settles on a later world tick (see above).
void Begin(const Request &req);

} // namespace EquipmentSet::Pending
