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

#include <vector>

namespace EquipmentSet::Data {

// Lazy: opens the WTF file on first call once the session globals are
// populated, parses, and caches. Safe to call from any Lua entry
// point — does nothing useful before the player picks a character.
void EnsureLoaded();

// Direct accessors. Both return null/zero before `EnsureLoaded`
// resolves a valid file path.
const std::vector<Set> &All();
const Set *FindByID(uint32_t setID);
const Set *FindByName(const char *name);
int IndexOf(uint32_t setID); // 0-based; -1 if missing

// Every set that includes the item instance `itemGuid`, in set order.
// Empty when the item belongs to no set, and for the reserved
// `GUID_EMPTY` / `GUID_IGNORED` sentinels — those mark a slot as empty
// or skipped, so they are never a real item and must not match.
//
// Matching is by GUID, so it identifies the one physical item that was
// saved into the set rather than any copy of the same itemID: a second
// Arcanite Reaper in the bag next to the set's own does not report as
// part of the set. Pointers reference `All()`'s storage and stay valid
// until the next mutation.
std::vector<const Set *> SetsContainingItem(uint64_t itemGuid);

// Mutations — bump-and-save semantics. Each returns the affected
// set's ID (or 0 on failure), persists to disk, and fires
// `EQUIPMENT_SETS_CHANGED`. Callers don't need to manage the file
// directly.
uint32_t Create(const char *name, const char *icon);
bool SaveExisting(uint32_t setID, const char *icon);
bool Rename(uint32_t setID, const char *newName);
bool Delete(uint32_t setID);

// Per-session "skip this slot on next save" state. Survives until the
// addon clears it or the game session ends; not persisted.
void IgnoreSlot(int slot1Based);
void UnignoreSlot(int slot1Based);
void ClearIgnoredSlots();
bool IsSlotIgnored(int slot1Based);

} // namespace EquipmentSet::Data
