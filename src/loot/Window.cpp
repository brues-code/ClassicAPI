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

#include "loot/Window.h"

#include "Game.h"
#include "Offsets.h"

namespace Loot::Window {

bool ReadSlot(int slot, Slot *out) {
    if (slot < 0 || slot >= Offsets::LOOT_MAX_SLOTS)
        return false;
    const uintptr_t row = Offsets::VAR_LOOT_SLOTS + slot * Offsets::LOOT_SLOT_STRIDE;
    out->itemID = Game::Read<uint32_t>(row);
    out->count = Game::Read<uint32_t>(row + Offsets::OFF_LOOT_SLOT_COUNT);
    out->wireIndex = Game::Read<uint8_t>(row + Offsets::OFF_LOOT_SLOT_WIRE_INDEX);
    return out->itemID != 0;
}

void SendLootItem(uint8_t wireIndex) {
    using SendLootItem_t = void(__stdcall *)(uint8_t wireIndex);
    reinterpret_cast<SendLootItem_t>(Offsets::FUN_CMSG_LOOT_ITEM)(wireIndex);
}

void Close(bool sendRelease) {
    using CloseLootInner_t =
        void(__fastcall *)(int sendRelease, char earlyReturnOnGameObject, char showError);
    reinterpret_cast<CloseLootInner_t>(Offsets::FUN_CLOSE_LOOT_INNER)(sendRelease ? 1 : 0, '\0',
                                                                     '\0');
}

} // namespace Loot::Window
