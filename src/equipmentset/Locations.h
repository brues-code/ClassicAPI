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

namespace EquipmentSet::Locations {

// True while the bank window is open (the engine's banker GUID at
// `VAR_BANK_GATE_GUID` is set) — 4.3.4's `CGBankInfo::m_banker`. The
// server only lets an item leave the bank while a banker is in range
// (`HandleSwapItem`'s `CanUseBank`), so a set treats bank items as
// usable only then.
bool BankerOpen();

// Searches the local player's paperdoll, backpack and bags — and the
// bank when `includeBank` — for an item whose GUID matches `targetGuid`,
// and returns the packed location code (see Set.h's
// `PackEquipped`/`PackBag`/`PackMainBank`/`PackBankBag`). Returns 0 if
// the GUID isn't found.
//
// Every location is read via the player invMgr's flat GUID array at
// `+0x04` and (for bag contents) via each bag's own invMgr reached
// through the bag's vtable `+0x10`. None of this goes through the
// engine's `GetItemBySlot` / `PackBagSlot` path, so no Lua stack is
// touched (safe to call from any callback), and bank GUIDs read even
// with the window closed (populated from server data at login).
int FindGUID(uint64_t targetGuid, bool includeBank);

// 4.3.4 `CGEquipmentManager::GetSetStatus`. Each non-empty slot counts
// once: ignored, worn (in ANY equipment slot), carried (bags, and the
// bank only while `BankerOpen`), else missing. `total` excludes
// ignored slots. `missingSlots[0..missing)` are 1-based slots.
struct Status {
    int total;
    int equipped;
    int inInventory;
    int missing;
    int ignored;
    int missingSlots[SLOT_COUNT];
};
Status GetStatus(const Set &set);

// 4.3.4 `CGEquipmentManager::IsSetEquipped`: every slot the set doesn't
// ignore holds exactly what the set saved — including empty for a slot
// saved empty.
bool IsEquipped(const Set &set);

// Resolves a GUID to a `CGItem*` via the engine's GUID→CObject
// resolver (`FUN_OBJECT_RESOLVE_BY_GUID`). Returns null if the item
// isn't known to the client. Side-effect free.
const uint8_t *ResolveItemByGUID(uint64_t guid);

// Copies the current paperdoll GUIDs into `out[0..SLOT_COUNT-1]`,
// where `out[i]` is the GUID at 1-based equipment slot `i+1`.
// Empty / unequipped slots write 0. Reads from the player invMgr's
// flat GUID array directly — no Lua stack, no engine state changes.
//
// Used by `UseEquipmentSet` to seed a "virtual paperdoll" model that
// stays current across in-flight swaps. The engine's CGItem state
// lags behind packet sends (server has to ACK each swap), so any
// subsequent `FindGUID` after the first packet would return stale
// pre-swap locations. Tracking moves locally bypasses that lag.
void SnapshotPaperdoll(uint64_t *out);

// True when any item of `set` the player still carries sits in the
// client-side in-transaction lock (a pending swap, mail / trade attach,
// or cursor pickup that an equip would race). Reads the CGItem instance
// flag `OFF_ITEM_CLIENT_LOCK` bit 0 — the bit `C_Item.IsLocked` checks.
//
// Gated on `FindGUID` BEFORE the lock flag: an item the player deleted,
// mailed or traded away can linger in the engine's CGItem cache with
// whatever lock bits it last had (often the in-transaction state of the
// delete itself), so reading it would report the set locked forever. A
// missing item is missing, not locked.
bool ContainsLockedItems(const Set &set);

// 4.3.4 `CGEquipmentManager::SetUsesLockedSlots`: whether an item worn in
// a slot the set would change (any slot it doesn't ignore) is locked. The
// action bar greys a set button on this as well as `ContainsLockedItems`.
bool UsesLockedSlots(const Set &set);

} // namespace EquipmentSet::Locations
