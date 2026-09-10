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

#include "item/Count.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Location.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>

namespace Item::Count {

namespace {

// Reads ITEM_FIELD_STACK_COUNT off a CGItem's m_objectFields
// descriptor at +0x20. Returns 0 if the descriptor is null. Stack
// counts are always positive — non-stackable items have count = 1.
int GetStackCount(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    auto *descriptor = Item::ObjectFields(cgItem);
    if (descriptor == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        descriptor + Offsets::OFF_DESCRIPTOR_STACK_COUNT));
}

// Per-item uses count for `includeUses` mode. Reads
// ITEM_FIELD_SPELL_CHARGES[0] (signed dword). Only negative values
// count — they mark "consume-on-use, destroyed at 0" items
// (Healthstone, Mana Gem, etc.). Positive or zero returns 1, so the
// multiplier is neutral for stacked consumables and ordinary items.
// Mirrors 3.3.5's `Script_GetItemCount` charge gate at
// `FUN_0051c2e0` — it only swaps to the charge-summing walker when
// `SPELL_CHARGES[0] < 0`.
int GetUsesPerItem(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 1;
    auto *descriptor = Item::ObjectFields(cgItem);
    if (descriptor == nullptr)
        return 1;
    const int32_t raw = *reinterpret_cast<const int32_t *>(
        descriptor + Offsets::OFF_DESCRIPTOR_SPELL_CHARGES_0);
    if (raw >= 0)
        return 1;
    return -raw;
}

// Combined per-slot contribution to GetItemCount. `includeUses=false`
// gives plain stack count (matches old behavior); `true` multiplies by
// abs(SPELL_CHARGES[0]) for charged items.
int GetItemContribution(const uint8_t *cgItem, bool includeUses) {
    const int stack = GetStackCount(cgItem);
    if (!includeUses)
        return stack;
    return stack * GetUsesPerItem(cgItem);
}

// Walks `bagID` and accumulates per-slot contributions for items
// matching `arg` (itemID, or name via `MatchesArg`). Skips empty slots.
// Each `ResolveBag` call clobbers Lua stack[1]/[2]; we own the stack
// inside the callback.
int CountInBag(void *L, int bagID, const Item::Arg::Resolved &arg, bool includeUses) {
    int total = 0;
    const int slotCount = Item::Location::GetBagSlotCount(bagID);
    for (int slot = 1; slot <= slotCount; slot++) {
        const uint8_t *item = Item::Location::ResolveBag(L, bagID, slot);
        if (item == nullptr)
            continue;
        if (!Item::Location::MatchesArg(item, arg))
            continue;
        total += GetItemContribution(item, includeUses);
    }
    return total;
}

// Direct GUID-array bypass for bank slots — sidesteps the gate at
// `0x006228C1` that `GetItemBySlot` applies to slots 39..68 when no
// bank window is open. The slot data in `invMgr+0x04` is populated at
// login (verified empirically — see `Offsets::OFF_INVMGR_GUID_ARRAY`),
// so we can read GUIDs directly and resolve them via the engine's own
// object resolver `FUN_OBJECT_RESOLVE_BY_GUID` (the same function
// `GetItemBySlot` would call internally if the gate let us through).

const uint8_t *ResolveByGuid(int type, uint64_t guid) {
    if (guid == 0)
        return nullptr;
    return static_cast<const uint8_t *>(
        Object::ByGuid(type, guid, "ItemMgr", 0x172));
}

// Walks linear slots `[firstSlot, lastSlot]` of `invMgr`'s GUID array
// at `+0x04`, resolves each non-zero GUID as a TYPE_ITEM, sums
// contributions of items matching `targetItemID`. Used for both the
// main bank (slots 39..62 in player invMgr) and individual bank-bag
// invMgrs (slots 0..N-1).
int CountInGuidArray(const uint8_t *invMgr, int firstSlot, int lastSlot,
                      const Item::Arg::Resolved &arg, bool includeUses) {
    if (invMgr == nullptr)
        return 0;
    auto *guidArray = *reinterpret_cast<const uint64_t *const *>(
        invMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (guidArray == nullptr)
        return 0;
    int total = 0;
    for (int slot = firstSlot; slot <= lastSlot; slot++) {
        const uint8_t *item = ResolveByGuid(Offsets::TYPEMASK_ITEM, guidArray[slot]);
        if (item == nullptr)
            continue;
        if (!Item::Location::MatchesArg(item, arg))
            continue;
        total += GetItemContribution(item, includeUses);
    }
    return total;
}

// For bank bag CONTENTS: the equipped bank bags themselves live at
// linear slots 63..68 in the player invMgr. Each is a CGContainer with
// its own invMgr accessed via `vtable[+0x10]` (`CGContainer::GetInvMgr`
// or similar). We resolve the bag, get its invMgr, and walk that.
//
// Layout: bag invMgr+0x00 = max slot count (the bag's size); bag
// invMgr+0x04 = its GUID array. Same shape as the player invMgr.
int CountInBankBags(const Item::Arg::Resolved &arg, bool includeUses) {
    auto *playerInvMgr = Unit::Identity::PlayerInventoryManager();
    if (playerInvMgr == nullptr)
        return 0;
    auto *playerGuidArray = *reinterpret_cast<const uint64_t *const *>(
        playerInvMgr + Offsets::OFF_INVMGR_GUID_ARRAY);
    if (playerGuidArray == nullptr)
        return 0;

    int total = 0;
    for (int slot = Offsets::INVMGR_BANK_BAG_FIRST_SLOT;
         slot <= Offsets::INVMGR_BANK_BAG_LAST_SLOT; slot++) {
        const uint8_t *bag = ResolveByGuid(Offsets::TYPEMASK_CONTAINER,
                                           playerGuidArray[slot]);
        if (bag == nullptr)
            continue;
        auto *bagInvMgr =
            static_cast<const uint8_t *>(Item::Location::ContainerInventory(bag));
        if (bagInvMgr == nullptr)
            continue;
        const int bagSlots = static_cast<int>(*reinterpret_cast<const uint32_t *>(bagInvMgr));
        if (bagSlots <= 0)
            continue;
        total += CountInGuidArray(bagInvMgr, 0, bagSlots - 1, arg, includeUses);
    }
    return total;
}

// Walks the player's 19 equipment slots and accumulates contributions
// for items matching `targetItemID`. Equipped items are part of the
// player's total inventory in the modern API — `GetItemCount` of a
// currently-equipped trinket returns 1, not 0.
int CountEquipped(const Item::Arg::Resolved &arg, bool includeUses) {
    int total = 0;
    for (int slot = 1; slot <= 19; slot++) {
        const uint8_t *item = Item::Location::ResolveEquipmentSlot(slot);
        if (item == nullptr)
            continue;
        if (!Item::Location::MatchesArg(item, arg))
            continue;
        total += GetItemContribution(item, includeUses);
    }
    return total;
}

// `C_Item.GetItemCount(itemInfo [, includeBank [, includeUses]])` —
// returns the player's total count of `itemInfo` across equipped
// items + bags (and optionally bank). `itemInfo` is a number, an
// `"item:NNN"` string / link, or an item NAME — a plain string is matched
// case-insensitively against each carried item's decorated display name
// (`Item::Location::MatchesArg`, the same rule `C_Item.UseItemByName`
// uses), which is what lets the `/cast` / `/use` handler decide
// "is this an item I carry" before falling back to a spell cast.
//
// What's counted:
//   - **Equipped items** (slots 1..19) — always. Matches modern
//     behavior: a currently-equipped trinket returns 1.
//   - **Bags 0..4** (backpack + 4 equipped bag slots) — always.
//     Live walk via `Item::Location::ResolveBag`.
//   - **Bank** (if `includeBank=true`): main bank (linear slots
//     39..62 in player invMgr) + each equipped bank bag's contents
//     (slots 63..68 in player invMgr). Bank walks **bypass
//     `GetItemBySlot`** by reading the GUID array at `invMgr+0x04`
//     directly and resolving via `FUN_OBJECT_RESOLVE_BY_GUID`. This
//     works without the bank window ever being opened — the GUIDs
//     are populated from server data at login. The engine gates
//     `GetItemBySlot` for bank slots via a banker-GUID check at
//     `VAR_BANK_GATE_GUID`, but the underlying data is always
//     present.
//
// `includeUses` (arg 3, boolean) multiplies each match by the item's
// `abs(SPELL_CHARGES[0])` — so a wand with 50 charges contributes 50
// instead of 1. Items without charges contribute their plain stack
// count (via the `or 1` neutralizer in `GetUsesPerItem`), so the flag
// is a no-op for them.
int __fastcall Script_C_Item_GetItemCount(void *L) {
    Item::Arg::Resolved arg = Item::Arg::Resolve(L, 1);
    // A plain string is a name. Copy it off the Lua string heap before the
    // bag walk — `ResolveBag` clears the stack.
    char nameBuf[128];
    if (arg.itemID <= 0) {
        if (arg.name == nullptr || arg.name[0] == '\0') {
            Game::Lua::PushNumber(L, 0);
            return 1;
        }
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", arg.name);
        arg.name = nameBuf;
    }
    // Read both flags before the bag walk — `ResolveBag` clobbers
    // the Lua stack via SetTop(0) and would invalidate any later
    // `ToBoolean` reads.
    const bool includeBank = Game::Lua::ToBoolean(L, 2) != 0;
    const bool includeUses = Game::Lua::ToBoolean(L, 3) != 0;

    int total = CountEquipped(arg, includeUses);
    for (int bag = 0; bag <= 4; bag++)
        total += CountInBag(L, bag, arg, includeUses);
    if (includeBank) {
        // Direct GUID-array reads — bypass the bank gate so this works
        // without the bank window having ever been opened in the
        // session.
        total += CountInGuidArray(Unit::Identity::PlayerInventoryManager(),
                                   Offsets::INVMGR_BANK_MAIN_FIRST_SLOT,
                                   Offsets::INVMGR_BANK_MAIN_LAST_SLOT,
                                   arg, includeUses);
        total += CountInBankBags(arg, includeUses);
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(total));
    return 1;
}

} // namespace

int InInventory(void *L, int itemID) {
    if (itemID <= 0)
        return 0;
    const Item::Arg::Resolved arg{itemID, 0, nullptr};
    int total = CountEquipped(arg, /*includeUses*/ false);
    for (int bag = 0; bag <= 4; bag++)
        total += CountInBag(L, bag, arg, /*includeUses*/ false);
    return total;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemCount",
                                     &Script_C_Item_GetItemCount);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Count
