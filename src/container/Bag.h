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

namespace Container::Bag {

// A container's storage, addressed without Lua: the inventory list holding
// its slot GUIDs, the linear index its first slot sits at, its slot count,
// and the BagFamily bitmask it accepts (0 = anything). The backpack and the
// main bank live inside the player's own inventory list at different linear
// bases; equipped bags and bank bags each have a list of their own.
struct View {
    const uint8_t *invMgr;
    int firstLinear;
    int slotCount;
    uint32_t family;
};

// Resolves Lua container `bagID` — backpack 0, bags 1..4, main bank -1, bank
// bags 5..10 — to its `View`. False for the keyring, an out-of-range ID, an
// empty bag slot, and any bank container while no banker is open. Pure C — no
// Lua stack.
bool ViewFor(int bagID, View *out);

// GUID of the item in 0-based slot `slotIndex0` of `view`, 0 when empty.
uint64_t SlotGuid(const View &view, int slotIndex0);

// Empty slots in `view`. A bag's own list is counted by the engine
// (`FUN_INVMGR_COUNT_FREE_SLOTS`, 3.3.5's `CGContainer_C::GetFreeSlots`); a
// range of the player's list (backpack, main bank) is walked slot by slot,
// the way 3.3.5's `Script_GetContainerNumFreeSlots` walks it.
int CountFree(const View &view);

} // namespace Container::Bag
