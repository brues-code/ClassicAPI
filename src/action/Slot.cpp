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

#include "action/Slot.h"

#include "Offsets.h"

namespace Action::Slot {

int MacroSlotForID(uint32_t macroID) {
    if (macroID == 0)
        return 0;
    auto *map = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_MACRO_SLOT_MAP));
    for (int i = 0; i < Offsets::MACRO_SLOT_MAP_COUNT; ++i) {
        if (map[i] == macroID)
            return i + 1;
    }
    return 0;
}

uint32_t MacroIDForSlot(int slot0) {
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return 0;
    auto *table = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ACTION_TABLE));
    const uint32_t entry = table[slot0];
    if (entry == 0 || (entry & Offsets::ACTION_TYPE_MASK) != Offsets::ACTION_TYPE_BAG_OR_MACRO)
        return 0;
    const uint32_t key = entry & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO;
    return MacroSlotForID(key) != 0 ? key : 0;
}

} // namespace Action::Slot
