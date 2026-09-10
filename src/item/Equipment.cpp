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

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Cursor.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/Swap.h"

#include <cstdint>

namespace Item::Equipment {

namespace {

// `OffhandHasWeapon()` — true iff the player has a one-handed
// weapon (or off-hand-only weapon) equipped in the off-hand slot.
// Returns false for empty off-hand, shields, and held items
// (tomes, orbs, librams) — even though the latter two share the
// off-hand slot.
//
// Implementation: resolves slot 17 → CGItem → itemID → cached
// ItemStats record → m_inventoryType. Falls back to false on any
// missing link in that chain (no item, item data not cached yet,
// etc.). Modern API behavior is the same — no async load fired,
// no event emitted; if the off-hand item isn't cached the function
// just returns false until something else warms the cache.
int __fastcall Script_OffhandHasWeapon(void *L) {
    auto *item = Item::Location::ResolveEquipmentSlot(Offsets::INVSLOT_OFFHAND);
    if (item == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }

    const int itemID = Item::ID::FromCGItem(item);
    if (itemID == 0) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    auto *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    const uint32_t invType = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    const bool isWeapon = (invType == Offsets::INVTYPE_WEAPON ||
                           invType == Offsets::INVTYPE_WEAPONOFFHAND);
    Game::Lua::PushBoolean(L, static_cast<int>(isWeapon));
    return 1;
}

// `C_Item.IsEquippableItem(item)` — true iff `item` can be equipped
// in *some* character-pane slot. Reads `m_inventoryType` off the
// cached ItemStats record and treats any non-zero value as
// equippable (INVTYPE_NON_EQUIP = 0, everything else 1..23 fits some
// slot). Modern signature accepts itemID number, `"item:N..."` link,
// or item name; we delegate input resolution to `Item::Arg::Resolve`.
//
// Cache-miss handling: returns false without firing a load (sync
// call, matches modern semantics). Callers that want auto-warm
// behavior should run `C_Item.RequestLoadItemDataByID` first and
// re-check on `ITEM_DATA_LOAD_RESULT`.
//
// Name-only inputs aren't supported because vanilla has no item-name
// → itemID resolver (`Item::Arg::Resolve` returns just a name string,
// no itemID, and we'd need to scan the entire ItemStats cache to
// match a name back — slow, and the engine doesn't ship that map).
// Modern WoW only resolves names for items currently in inventory;
// `C_Item.IsEquippedItem` covers that exact niche already.
int __fastcall Script_C_Item_IsEquippableItem(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    auto *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const uint32_t invType = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    Game::Lua::PushBoolean(L, invType > 0);
    return 1;
}

// `C_Item.IsEquippedItem(item)` — true iff any character-pane equipment
// slot (1..19) currently holds an item matching `item`. Modern API
// accepts:
//   - itemID number → exact match against the equipped slot's itemID
//   - "item:N..." string → parse the link's first numeric field as the
//                          itemID and match the same way
//   - plain string → case-insensitive name match against the cached
//                    `m_name[0]` of each equipped item
//
// Returns false (never errors) for invalid input, an empty inventory,
// or an uncached match candidate. Walks slots in order and short-
// circuits on the first hit.
int __fastcall Script_C_Item_IsEquippedItem(void *L) {
    const auto arg = Item::Arg::Resolve(L, 1);
    if (arg.itemID <= 0 && arg.name == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }

    // Equipment-first walk; itemID or decorated-name match via the shared
    // `Item::Location::MatchesArg` predicate (same logic the bag find-by-
    // name uses, so match semantics live in one place).
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST; slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        auto *item = Item::Location::ResolveEquipmentSlot(slot);
        if (item == nullptr) continue;
        if (Item::Location::MatchesArg(item, arg)) {
            Game::Lua::PushBoolean(L, 1);
            return 1;
        }
    }

    Game::Lua::PushBoolean(L, 0);
    return 1;
}

// `C_Item.EquipItemByName(item [, dstSlot])` — equips `item`.
//
// `item` is an item reference (itemID, `item:N`, a link, or the name of an
// item the player carries — the first bag match is equipped) or an item
// location (`{bagID=B, slotIndex=S}`, `{equipmentSlotIndex=N}`, an item
// GUID), which names one exact item. See
// `Item::Location::FindItemArgOrLocation`, shared with the item-use APIs;
// this call wants the source coordinates too, since the swap packet carries
// them.
//
// Two paths after a shared cursor-clear:
//
// - **Explicit `dstSlot`** (1..19): cursor-free direct swap via the engine's
//   `FUN_INVENTORY_SWAP` primitive — atomic server-side, no pickup.
//   `Item::Swap::FromBag` for an item in a bag, `FromPaperdoll` for one
//   already equipped (an `{equipmentSlotIndex=N}` location, which is how a
//   ring 11↔12 or weapon 16↔17 move is expressed).
//
// - **No `dstSlot`** (engine auto-picks slot from INVTYPE): cursor-
//   pickup + `AutoEquipCursorItem`, because 1.12's auto-pick logic
//   reads its slot decision off cursor state. An already-equipped item has
//   nothing to auto-pick, so that combination does nothing.
//
// Both paths start with `ClearCursor()` to clear any preexisting
// cursor state. If something was on the cursor, it gets returned
// to its source slot (visual lock cleared, source globals reset)
// before our work runs. Without this, the engine's swap primitive
// ends each call with a cursor-state cleanup that skips the
// item-flag clear (`FUN_00495190(0, 1)`); a held item would stay
// visually locked until a relog refreshed inventory state.
//
// The item is resolved AFTER that clear, so a location naming the slot a
// held item came from resolves to it (its slot reads empty while held) and a
// name search sees the returned item too. `Script_ClearCursor` is
// `FUN_00495190(1, 1)` and nothing else — it takes no arguments and reads no
// Lua stack — so our own arguments survive it.
//
// Returns nothing. Silently no-ops on bad/missing input, an item the player
// doesn't have, or the engine refusing the swap (combat lockdown,
// item-locked flag, type mismatch).
int __fastcall Script_C_Item_EquipItemByName(void *L) {
    const bool hasDstSlot = Game::Lua::IsNumber(L, 2);
    const int dstSlot = hasDstSlot ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 0;

    // Return any held cursor item to its slot BEFORE resolving the target.
    // FUN_INVENTORY_SWAP's end-of-call cleanup at FUN_00495190(0, 1) clears
    // LOCAL cursor globals but skips the visual-lock-flag clear
    // (`item+0x314 & ~1`) — so a held item would stay visually locked until
    // a relog refreshed state. ClearCursor's FUN_00495190(1, 1) includes it.
    reinterpret_cast<int(__fastcall *)(void *)>(
        Offsets::FUN_SCRIPT_CLEAR_CURSOR)(L);

    Item::Location::ByGUIDResult found;
    if (!Item::Location::FindItemArgOrLocation(L, 1, &found)) {
        return 0;
    }

    if (hasDstSlot) {
        if (found.equipmentSlotIndex != 0)
            Item::Swap::FromPaperdoll(found.item, found.equipmentSlotIndex, dstSlot);
        else
            Item::Swap::FromBag(found.item, found.bagID, found.slotIndex, dstSlot);
        return 0;
    }
    if (found.equipmentSlotIndex != 0)
        return 0; // already equipped — no slot for the engine to pick

    // Auto-slot path: pick the item up onto the cursor, then call the
    // engine's `CGPlayer::AutoEquipCursorItem` helper directly
    // (`Script_AutoEquipCursorItem` is a thin wrapper that just resolves
    // the local player and calls this with flag=0). The pickup goes through
    // the shared `Item::Cursor::PickupBagItem`, which drives the engine's
    // cursor primitive directly (no Script_PickupContainerItem stack-fake);
    // its empty-cursor guard is satisfied by the ClearCursor above. Bail if
    // the pickup didn't take (locked item / busy cursor) — nothing to equip.
    if (!Item::Cursor::PickupBagItem(L, found.bagID, found.slotIndex))
        return 0;

    using AutoEquipCursor_t = void (__thiscall *)(void *player, int flag);
    if (auto *player = Game::ResolveUnitToken("player")) {
        auto equip = reinterpret_cast<AutoEquipCursor_t>(
            Offsets::FUN_AUTO_EQUIP_CURSOR_ITEM);
        equip(player, 0);
    }
    return 0;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("OffhandHasWeapon", &Script_OffhandHasWeapon);
    Game::Lua::RegisterTableFunction("C_Item", "IsEquippableItem",
                                     &Script_C_Item_IsEquippableItem);
    Game::Lua::RegisterTableFunction("C_Item", "IsEquippedItem", &Script_C_Item_IsEquippedItem);
    Game::Lua::RegisterTableFunction("C_Item", "EquipItemByName", &Script_C_Item_EquipItemByName);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Equipment
