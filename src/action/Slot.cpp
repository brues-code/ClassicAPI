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
#include "action/Extended.h"
#include "macro/Slot.h"

namespace Action::Slot {

namespace {

using SlotToSpell_t = uint32_t(__fastcall *)(uint32_t slot0, uint32_t *outBook);

} // namespace

bool Valid(int slot0) {
    return (slot0 >= 0 && slot0 < Offsets::ACTION_TABLE_MAX_SLOTS) || Extended::Owns(slot0);
}

uint32_t Word(int slot0) {
    if (slot0 >= 0 && slot0 < Offsets::ACTION_TABLE_MAX_SLOTS)
        return reinterpret_cast<const uint32_t *>(
            static_cast<uintptr_t>(Offsets::VAR_ACTION_TABLE))[slot0];
    return Extended::Get(slot0);
}

uint32_t SpellFor(int slot0, uint32_t *book) {
    if (slot0 >= 0 && slot0 < Offsets::ACTION_TABLE_MAX_SLOTS)
        return reinterpret_cast<SlotToSpell_t>(Offsets::FUN_ACTION_SLOT_TO_SPELL)(
            static_cast<uint32_t>(slot0), book);
    return Extended::SpellFor(Extended::Get(slot0), book);
}

int MacroSlotForID(uint32_t macroID) {
    if (macroID == 0)
        return 0;
    // The engine's own scan: the 0-based slot, or 0xFFFFFFFF on a miss.
    const uint32_t slot0 = ::Macro::Slot::ForId(macroID);
    return slot0 == ::Macro::Slot::kMiss ? 0 : static_cast<int>(slot0) + 1;
}

uint32_t MacroIDForSlot(int slot0) {
    const uint32_t entry = Word(slot0);
    if (entry == 0 || (entry & Offsets::ACTION_TYPE_MASK) != Offsets::ACTION_TYPE_BAG_OR_MACRO)
        return 0;
    const uint32_t key = entry & Offsets::ACTION_PAYLOAD_MASK_BAG_OR_MACRO;
    return MacroSlotForID(key) != 0 ? key : 0;
}

} // namespace Action::Slot
