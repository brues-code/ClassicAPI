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

// The open loot window: the engine's slot table (`VAR_LOOT_SLOTS`, filled by
// the loot controller) and the two engine calls that act on it.
namespace Loot::Window {

// One row of the slot table.
struct Slot {
    uint32_t itemID;
    uint32_t count;
    uint8_t wireIndex; // the server's slot index, what CMSG_LOOT_ITEM names
};

// Row `slot` (0-based, below LOOT_MAX_SLOTS) of the open window. False for a
// slot out of range and for an empty row (itemID 0: coin, or nothing).
bool ReadSlot(int slot, Slot *out);

// Sends CMSG_LOOT_ITEM for `wireIndex` (FUN_CMSG_LOOT_ITEM, `__stdcall`, `ret
// 4`). It skips the BoP / unique confirm that the Lua `LootSlot` raises; the
// server still checks loot rights.
void SendLootItem(uint8_t wireIndex);

// Closes the window (FUN_CLOSE_LOOT_INNER with no early return and no error):
// clears the loot GUID and the slot table and fires LOOT_CLOSED.
// `sendRelease` also sends CMSG_LOOT_RELEASE.
void Close(bool sendRelease);

} // namespace Loot::Window
