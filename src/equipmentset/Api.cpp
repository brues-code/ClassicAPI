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

// `C_EquipmentSet.*` — backports the modern equipment-set API on top
// of a client-side persistent store. Vanilla 1.12 has no server-side
// equipment-set storage (no CMSG_EQUIPMENT_SET_SAVE opcode), so we
// persist each character's sets to
// `WTF\Account\<acct>\<realm>\<char>\ClassicAPI_EquipmentSets.txt`
// the same way `VanillaMinimapTracking` keeps its tracking config.
//
// Identity is by item GUID — `SaveEquipmentSet` snapshots the GUIDs
// currently worn, and `UseEquipmentSet` searches every player-owned
// container for those GUIDs at use time. The GUID search reads
// invMgr GUID arrays directly (same trick `C_Item.GetItemCount`
// uses), so bank items resolve without the bank window being open.
//
// Modern Classic Era's signatures use a numeric `iconFileID`; we
// accept icon paths (e.g. `"INV_Shield_06"`) instead because vanilla
// has no fileDataID system. Same string-or-default fallback semantic
// as 4.3.4's native `SaveEquipmentSet`.

#include "Action.h"
#include "Data.h"
#include "Game.h"
#include "Locations.h"
#include "Offsets.h"
#include "Set.h"
#include "event/Custom.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "item/Swap.h"

#include <cstdint>
#include <cstring>

namespace EquipmentSet::Api {

namespace {

// Pulls `setID` off Lua stack[idx]. Accepts numbers only — modern
// signatures take numeric IDs, not names, for the by-ID functions.
// Returns 0 if missing/non-numeric (callers treat 0 as "no such set").
uint32_t ArgSetID(void *L, int idx) {
    if (!Game::Lua::IsNumber(L, idx))
        return 0;
    const double d = Game::Lua::ToNumber(L, idx);
    if (d < 1.0)
        return 0;
    return static_cast<uint32_t>(d);
}

int ArgInt(void *L, int idx, int fallback = 0) {
    if (!Game::Lua::IsNumber(L, idx))
        return fallback;
    return static_cast<int>(Game::Lua::ToNumber(L, idx));
}

const char *ArgString(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return Game::Lua::ToString(L, idx);
}

int ResolveItemID(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    auto *instance = Item::InstanceBlock(cgItem);
    if (instance == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID));
}

void PushTableEntry(void *L, int key, double value) {
    Game::Lua::PushNumber(L, static_cast<double>(key));
    Game::Lua::PushNumber(L, value);
    Game::Lua::SetTable(L, -3);
}

// 1.12 has no concept of "uncomingable equipment-set support", so
// `CanUseEquipmentSets` is always true (we mirror what Classic Era
// 1.15 does — it returns true unconditionally there as well).
int __fastcall Script_CanUseEquipmentSets(void *L) {
    Game::Lua::PushBool(L, 1);
    return 1;
}

int __fastcall Script_GetNumEquipmentSets(void *L) {
    const auto &all = Data::All();
    Game::Lua::PushNumber(L, static_cast<double>(all.size()));
    return 1;
}

// Modern returns a numeric-keyed array of setIDs. Order matches the
// list's storage order (insertion order; no implicit alphabetical
// sort). Empty list returns an empty table.
int __fastcall Script_GetEquipmentSetIDs(void *L) {
    const auto &all = Data::All();
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int key = 1;
    for (const auto &s : all) {
        Game::Lua::PushNumber(L, static_cast<double>(key++));
        Game::Lua::PushNumber(L, static_cast<double>(s.setID));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_GetEquipmentSetID(void *L) {
    const char *name = ArgString(L, 1);
    if (name == nullptr)
        return 0;
    const Set *s = Data::FindByName(name);
    if (s == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(s->setID));
    return 1;
}

// Returns the 9-tuple Blizzard ships:
//   name, icon, setID, isEquipped, numItems, numEquipped,
//   numInInventory, numMissing, numIgnored
//
// `numItems` excludes ignored slots — matches modern semantics where
// an ignored slot is "not part of the set", not "part of the set but
// unfilled". `isEquipped` is the boolean equivalent of
// `numEquipped == numItems`; modern flips it true even when missing
// items exist, as long as everything currently *resolvable* is in its
// target slot. We follow that — strict-equipped semantics break in
// useful cases (e.g. you're at a bank without your bank-stored cloak).
int __fastcall Script_GetEquipmentSetInfo(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;

    int numItems = 0;
    int numEquipped = 0;
    int numInInventory = 0;
    int numMissing = 0;
    int numIgnored = 0;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = s->items[i];
        if (g == GUID_IGNORED) {
            ++numIgnored;
            continue;
        }
        if (g == GUID_EMPTY)
            continue;
        ++numItems;
        const int loc = Locations::FindGUID(g);
        if (loc == 0) {
            ++numMissing;
        } else if ((loc & LOC_BAGS) == 0 && (loc & LOC_BANK) == 0 &&
                   (loc & LOC_SLOT_MASK) == (i + 1)) {
            ++numEquipped;
        } else {
            ++numInInventory;
        }
    }

    Game::Lua::PushString(L, s->name.c_str());
    Game::Lua::PushString(L, s->icon.c_str());
    Game::Lua::PushNumber(L, static_cast<double>(s->setID));
    Game::Lua::PushBoolean(L, (numItems > 0 && numEquipped + numMissing == numItems &&
                               numInInventory == 0));
    Game::Lua::PushNumber(L, static_cast<double>(numItems));
    Game::Lua::PushNumber(L, static_cast<double>(numEquipped));
    Game::Lua::PushNumber(L, static_cast<double>(numInInventory));
    Game::Lua::PushNumber(L, static_cast<double>(numMissing));
    Game::Lua::PushNumber(L, static_cast<double>(numIgnored));
    return 9;
}

int __fastcall Script_GetIgnoredSlots(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int key = 1;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (s->items[i] == GUID_IGNORED) {
            Game::Lua::PushNumber(L, static_cast<double>(key++));
            Game::Lua::PushNumber(L, static_cast<double>(i + 1));
            Game::Lua::SetTable(L, -3);
        }
    }
    return 1;
}

// Modern returns a hash table { [slot] = itemID } with one entry per
// non-empty, non-ignored slot. Missing items still get an entry — but
// since we can't recover an itemID from a GUID alone after the item
// has left the client (e.g. mailed away), missing slots are omitted.
int __fastcall Script_GetItemIDs(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = s->items[i];
        if (g == GUID_EMPTY || g == GUID_IGNORED)
            continue;
        const uint8_t *item = Locations::ResolveItemByGUID(g);
        const int itemID = ResolveItemID(item);
        if (itemID == 0)
            continue;
        PushTableEntry(L, i + 1, static_cast<double>(itemID));
    }
    return 1;
}

// Hash table { [slot] = locationCode } — same encoding modern uses,
// see Set.h's pack helpers. Ignored slots produce `LOC_IGNORED`,
// missing items produce `LOC_MISSING`, found items produce the
// packed `LOC_PLAYER|...|slot` code. Empty (uninitialized) slots
// don't appear in the table.
int __fastcall Script_GetItemLocations(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = s->items[i];
        if (g == GUID_EMPTY)
            continue;
        int loc;
        if (g == GUID_IGNORED) {
            loc = LOC_IGNORED;
        } else {
            loc = Locations::FindGUID(g);
            if (loc == 0)
                loc = LOC_MISSING;
        }
        PushTableEntry(L, i + 1, static_cast<double>(loc));
    }
    return 1;
}

int __fastcall Script_CreateEquipmentSet(void *L) {
    const char *name = ArgString(L, 1);
    if (name == nullptr || name[0] == '\0') {
        Game::Lua::Error(L, "Usage: C_EquipmentSet.CreateEquipmentSet(name [, icon])");
        return 0;
    }
    const char *icon = ArgString(L, 2);
    const uint32_t setID = Data::Create(name, icon);
    if (setID == 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(setID));
    return 1;
}

int __fastcall Script_SaveEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    if (setID == 0) {
        Game::Lua::Error(L, "Usage: C_EquipmentSet.SaveEquipmentSet(setID [, icon])");
        return 0;
    }
    const char *icon = ArgString(L, 2);
    if (Data::SaveExisting(setID, icon))
        Action::Repaint(setID); // the icon may have changed
    return 0;
}

int __fastcall Script_ModifyEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const char *newName = ArgString(L, 2);
    if (setID == 0 || newName == nullptr || newName[0] == '\0') {
        Game::Lua::Error(L,
                         "Usage: C_EquipmentSet.ModifyEquipmentSet(setID, newName)");
        return 0;
    }
    if (Data::Rename(setID, newName))
        Action::Repaint(setID); // buttons show the name
    return 0;
}

int __fastcall Script_DeleteEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    if (setID == 0) {
        Game::Lua::Error(L, "Usage: C_EquipmentSet.DeleteEquipmentSet(setID)");
        return 0;
    }
    if (Data::Delete(setID))
        Action::ClearButtons(setID);
    return 0;
}

int __fastcall Script_IgnoreSlotForSave(void *L) {
    const int slot = ArgInt(L, 1);
    Data::IgnoreSlot(slot);
    return 0;
}

int __fastcall Script_UnignoreSlotForSave(void *L) {
    const int slot = ArgInt(L, 1);
    Data::UnignoreSlot(slot);
    return 0;
}

int __fastcall Script_IsSlotIgnoredForSave(void *L) {
    const int slot = ArgInt(L, 1);
    Game::Lua::PushBool(L, Data::IsSlotIgnored(slot));
    return 1;
}

int __fastcall Script_ClearIgnoredSlotsForSave(void *L) {
    (void)L;
    Data::ClearIgnoredSlots();
    return 0;
}

// Whether a swap would race an item already in transit — see
// `Locations::ContainsLockedItems`.
int __fastcall Script_EquipmentSetContainsLockedItems(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr)
        return 0;
    Game::Lua::PushBool(L, Locations::ContainsLockedItems(*s));
    return 1;
}

// Walks the set and dispatches one atomic server-side swap per item
// that isn't already in its target slot. Items currently in the main
// bank or bank bags are skipped silently — vanilla rejects equip
// from bank slots server-side, so the user needs to retrieve them
// first.
//
// Cycle handling: with cursor-based pickup+equip, swap cycles (e.g.
// rings A↔B wanting each other's slots) needed a temp-bag-slot
// shuffle. We now call the engine's own `FUN_INVENTORY_SWAP` per
// item, which sends opcode 0x10D / 0x10C through the engine's
// packet pipeline — an atomic server-side swap. A two-cycle resolves
// in one call (server swaps both slots from one packet); the cursor
// is never touched. See TODO #59 for the primitive's decoding and
// TODO #73 for the cycle-resolution rationale.
int __fastcall Script_UseEquipmentSet(void *L) {
    const uint32_t setID = ArgSetID(L, 1);
    const Set *s = Data::FindByID(setID);
    if (s == nullptr) {
        const int evt = kEvtSwapFinished.Slot();
        if (evt >= 0)
            Event::Custom::Fire(evt, "%d%d", 0, static_cast<int>(setID));
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    // Fire PENDING right after the set-exists check, before any of
    // the swap work. Addon UI can use this to gate swap-in-progress
    // visuals.
    const int pendingEvt = kEvtSwapPending.Slot();
    if (pendingEvt >= 0)
        Event::Custom::Fire(pendingEvt, "%d", static_cast<int>(setID));

    // If the cursor is holding anything, return it to its source slot
    // and unlock it server-side BEFORE our swap chain runs. The
    // engine's FUN_INVENTORY_SWAP ends each call with a cursor-state
    // cleanup that clears the LOCAL cursor view, but never sends the
    // server-side cancel-pickup packet — so a held item would stay
    // server-locked until relog. ClearCursor sends the right packet.
    // No-op when the cursor is idle (still fires one CURSOR_UPDATE,
    // negligible).
    Game::Lua::SetTop(L, 0);
    reinterpret_cast<int(__fastcall *)(void *)>(
        Offsets::FUN_SCRIPT_CLEAR_CURSOR)(L);

    // Snapshot the set's items into a local — Locations::FindGUID
    // can shuffle the Lua stack, and we've already passed validation.
    uint64_t guids[SLOT_COUNT];
    std::memcpy(guids, s->items, sizeof(guids));

    // Snapshot empty bag slots up front. The CGItem state doesn't
    // update between packet sends (server has to acknowledge each
    // swap), so iterating the bag walk fresh per unequip would
    // repeatedly pick the same "first empty" slot — second packet
    // collides with what packet 1 just put there, and the server
    // tries to swap that item back onto the wrong paperdoll slot
    // → "This item cannot be equipped".
    struct FreeSlot {
        int bag;
        int slot;
    };
    FreeSlot freeSlots[64];
    int freeSlotCount = 0;
    for (int bag = 0; bag <= 4 && freeSlotCount < 64; ++bag) {
        const int slots = Item::Location::GetBagSlotCount(bag);
        for (int slot = 1; slot <= slots && freeSlotCount < 64; ++slot) {
            if (Item::Location::ResolveBag(L, bag, slot) == nullptr)
                freeSlots[freeSlotCount++] = {bag, slot};
        }
    }
    int nextFreeSlot = 0;

    // Pre-snapshot each set item's location against the ORIGINAL
    // state, before any swap packets fly. This mirrors how 4.3.4's
    // `Script_UseEquipmentSet` (`0x009A15D0`) builds its action list:
    // resolve all 19 source locations up front, then execute. The
    // engine's CGItem / InvMgr state lags behind our packet sends
    // (waits for server ACK), so any `FindGUID` mid-execution would
    // return stale answers and we'd re-resolve items into the wrong
    // place.
    int sourceLoc[SLOT_COUNT];
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint64_t g = guids[i];
        if (g == GUID_EMPTY || g == GUID_IGNORED) {
            sourceLoc[i] = 0;
            continue;
        }
        sourceLoc[i] = Locations::FindGUID(g);
    }

    // Virtual paperdoll occupant model — seeded from the engine's
    // initial paperdoll GUID array, then updated in-place after each
    // swap so phase 1 can chain rotations and phase 3 can spot
    // already-empty slots.
    uint64_t virtualPaperdoll[SLOT_COUNT];
    Locations::SnapshotPaperdoll(virtualPaperdoll);

    // Locates the current virtual slot of a GUID on the paperdoll,
    // or 0 if it's not currently there. Used during phase 1 so
    // chained paperdoll-to-paperdoll moves operate on the *current*
    // virtual position, not the stale pre-snapshot.
    auto currentPaperdollSlot = [&](uint64_t g) -> int {
        if (g == 0)
            return 0;
        for (int slot = 1; slot <= SLOT_COUNT; ++slot) {
            if (virtualPaperdoll[slot - 1] == g)
                return slot;
        }
        return 0;
    };

    // ── Phase 1: paperdoll → paperdoll ─────────────────────────────
    // Move items already on the paperdoll into their target slots.
    // Source and destination are both paperdoll, so atomic swaps
    // never touch bag state — phase 2's bag lookups stay accurate.
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int targetSlot = i + 1;
        const uint64_t g = guids[i];
        if (g == GUID_EMPTY || g == GUID_IGNORED)
            continue;

        const int loc = sourceLoc[i];
        if (loc == 0)
            continue;
        if (loc & LOC_BANK)
            continue; // server rejects equip-from-bank
        if (loc & LOC_BAGS)
            continue; // phase 2

        const int srcSlot = currentPaperdollSlot(g);
        if (srcSlot == 0 || srcSlot == targetSlot)
            continue; // missing or already there

        const uint8_t *item = Locations::ResolveItemByGUID(g);
        if (item == nullptr)
            continue;

        const uint64_t displaced = virtualPaperdoll[targetSlot - 1];
        if (Item::Swap::FromPaperdoll(item, srcSlot, targetSlot)) {
            virtualPaperdoll[targetSlot - 1] = g;
            virtualPaperdoll[srcSlot - 1] = displaced;
        }
    }

    // ── Phase 2: bag → paperdoll ───────────────────────────────────
    // Move items from bag slots onto the paperdoll. Each atomic swap
    // displaces the current paperdoll occupant into the bag slot the
    // incoming item vacated. Anything we still wanted on the paperdoll
    // was already placed by phase 1, so phase 2 displacements can't
    // strand a future set item in a bag.
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int targetSlot = i + 1;
        const uint64_t g = guids[i];
        if (g == GUID_EMPTY || g == GUID_IGNORED)
            continue;
        if (virtualPaperdoll[targetSlot - 1] == g)
            continue; // already at target (probably by phase 1)

        const int loc = sourceLoc[i];
        if (loc == 0)
            continue;
        if (loc & LOC_BANK)
            continue;
        if ((loc & LOC_BAGS) == 0)
            continue; // not from a bag — phase 1 handled, or item missing

        const uint8_t *item = Locations::ResolveItemByGUID(g);
        if (item == nullptr)
            continue;

        const int srcBag = (loc >> LOC_BAG_SHIFT) & 0xFF;
        const int srcSlot = loc & LOC_SLOT_MASK;
        if (Item::Swap::FromBag(item, srcBag, srcSlot, targetSlot)) {
            virtualPaperdoll[targetSlot - 1] = g;
            // The displaced item lands in (srcBag, srcSlot). We
            // don't track bag-side virtual state — by phase 3 we
            // only care about the paperdoll occupant.
        }
    }

    // ── Phase 3: unequip slots the set wants empty ─────────────────
    // For each `GUID_EMPTY` slot, send the current paperdoll occupant
    // (if any) to a pre-snapshotted free bag slot. Runs last so any
    // displaced items from phases 1/2 that landed in the slot get
    // unequipped here too.
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int targetSlot = i + 1;
        if (guids[i] != GUID_EMPTY)
            continue;
        const uint64_t occupant = virtualPaperdoll[targetSlot - 1];
        if (occupant == 0)
            continue;
        if (nextFreeSlot >= freeSlotCount)
            continue;
        const uint8_t *equipped = Locations::ResolveItemByGUID(occupant);
        if (equipped == nullptr)
            continue;
        const FreeSlot &fs = freeSlots[nextFreeSlot++];
        if (Item::Swap::ToBag(equipped, targetSlot, fs.bag, fs.slot))
            virtualPaperdoll[targetSlot - 1] = 0;
    }

    const int evt = kEvtSwapFinished.Slot();
    if (evt >= 0)
        Event::Custom::Fire(evt, "%d%d", 1, static_cast<int>(setID));

    Game::Lua::PushBoolean(L, 1);
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "CanUseEquipmentSets",
                                     &Script_CanUseEquipmentSets);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetNumEquipmentSets",
                                     &Script_GetNumEquipmentSets);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetEquipmentSetIDs",
                                     &Script_GetEquipmentSetIDs);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetEquipmentSetID",
                                     &Script_GetEquipmentSetID);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetEquipmentSetInfo",
                                     &Script_GetEquipmentSetInfo);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetIgnoredSlots",
                                     &Script_GetIgnoredSlots);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetItemIDs",
                                     &Script_GetItemIDs);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "GetItemLocations",
                                     &Script_GetItemLocations);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "CreateEquipmentSet",
                                     &Script_CreateEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "SaveEquipmentSet",
                                     &Script_SaveEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "ModifyEquipmentSet",
                                     &Script_ModifyEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "DeleteEquipmentSet",
                                     &Script_DeleteEquipmentSet);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "IgnoreSlotForSave",
                                     &Script_IgnoreSlotForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "UnignoreSlotForSave",
                                     &Script_UnignoreSlotForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "IsSlotIgnoredForSave",
                                     &Script_IsSlotIgnoredForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "ClearIgnoredSlotsForSave",
                                     &Script_ClearIgnoredSlotsForSave);
    Game::Lua::RegisterTableFunction("C_EquipmentSet",
                                     "EquipmentSetContainsLockedItems",
                                     &Script_EquipmentSetContainsLockedItems);
    Game::Lua::RegisterTableFunction("C_EquipmentSet", "UseEquipmentSet",
                                     &Script_UseEquipmentSet);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace EquipmentSet::Api
