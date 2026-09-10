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

namespace Item::Location {

// Resolves an item-location arg at Lua stack index `locIdx` to a `CGItem *`
// pointer (returned as a raw byte pointer for use with `OFF_*` offsets).
//
// Accepts:
//   - location tables: `{ equipmentSlotIndex = N }` or
//     `{ bagID = B, slotIndex = S }` — same shapes the modern
//     `ItemLocation` mixin uses.
//   - GUID strings: `"0xHHHHHHHHLLLLLLLL"` — the form
//     `C_Item.GetItemGUID` returns. The GUID is looked up by walking
//     equipment slots 1..19 + bags 0..4; bank and keyring are not
//     walked.
//
// Returns `nullptr` when the value isn't a valid location arg, the
// table doesn't carry recognizable fields, the GUID is malformed, or
// the resolved slot is empty / the GUID isn't resident. Does NOT
// raise a Lua error — callers should validate the argument type
// beforehand with `IsLocationArg` if they want a typed error.
//
// Both paths may stomp the Lua stack (the bag-form table uses
// `PackBagSlot`; the GUID path iterates bag slots via the same
// helper). Only safe to call from inside a Lua callback.
const uint8_t *Resolve(void *L, int locIdx);

// Type-check helper: returns true if the value at `idx` could be a
// valid location arg (a table or a string). Used by every C_Item.*
// function that takes a location, to validate before calling
// `Resolve`. Does not dereference the value.
bool IsLocationArg(void *L, int idx);

// Resolves a `(bagID, slotIndex)` pair directly to a `CGItem *`. Used by
// the positional-arg C_Container.* APIs that don't go through the
// ItemLocation table shape. Stomps the Lua stack — same caveat as
// `Resolve()` above.
const uint8_t *ResolveBag(void *L, int bagID, int slotIndex);

// Resolves a 1-based character-pane equipment slot directly to a
// `CGItem *`. Used by the positional-arg `GetInventoryItemDurability`
// (and any future per-slot getter) that doesn't take an ItemLocation
// table. Player-only — walks the local player's private inventory
// manager, same path the `{equipmentSlotIndex=N}` table form uses.
// Returns nullptr for out-of-range slots or empty equipment.
const uint8_t *ResolveEquipmentSlot(int slot1Based);

// Returns the number of slots in player bag `bagID` (0..4) without
// going through `Script_GetContainerNumSlots` — bag 0 is the fixed
// 16-slot backpack, bags 1..4 read `m_containerSlots` off the
// equipped bag's cached `ItemStats_C` record. Returns 0 for empty
// bag slots and out-of-range `bagID`. No Lua stack interaction —
// safe to call outside a callback. Equipped-bag ItemStats are
// guaranteed cached by the engine's bag-sync flow at login.
int GetBagSlotCount(int bagID);

// Given a `CGContainer*` (an equipped bag or bank bag, resolved by GUID via
// `FUN_OBJECT_RESOLVE_BY_GUID` with `TYPEMASK_CONTAINER`), returns its own
// inventory-manager object via the container's vtable method at
// `+OFF_CONTAINER_GET_INVENTORY`. The returned object has the same layout as
// the player inventory manager: slot count at `+0x00`, flat GUID array at
// `+OFF_INVMGR_GUID_ARRAY`. Null-safe (null container → null). Pure C — no
// Lua stack, safe from any context. Consolidates the vtable dispatch that was
// hand-rolled across the bag/bank walks in `item/Count`, `item/AverageLevel`,
// `equipmentset/Locations`, and `item/NewItems`.
void *ContainerInventory(const uint8_t *container);

// Returns the inventory object of the equipped bag in player bag slot `bagID`
// (1..4): its container GUID via `FUN_GET_EQUIPPED_BAG_GUID`, resolved to the
// CGContainer, then its inventory via `ContainerInventory`. The result has the
// player-invMgr shape (slot count at `+0x00`, GUID array at
// `+OFF_INVMGR_GUID_ARRAY`) and is what `GetItemBySlot` indexes with a 0-based
// slot. Returns null for `bagID` outside 1..4 or when no bag is equipped
// there. Pure C — no Lua stack. (The backpack, `bagID` 0, has no container
// object; index it directly off the player inventory manager at
// `BACKPACK_LINEAR_BASE + slot - 1`.)
void *EquippedBagInventory(int bagID);

// GUID-walk result. `equipmentSlotIndex != 0` means the item was
// found in a character-pane equipment slot; otherwise it's in
// `bagID`/`slotIndex`. `item` always points to the found CGItem
// when the function returns true.
struct ByGUIDResult {
    int equipmentSlotIndex; // 1..19, or 0 if bag form
    int bagID;
    int slotIndex;
    const uint8_t *item;
};

// Walks the local player's inventory (equipment 1..19 + bags 0..4)
// looking for a CGItem with a matching GUID. Returns false when the
// GUID isn't found, true (and fills `*out`) on a hit. Stomps the
// Lua stack — only safe from inside a Lua callback.
bool FindByGUID(void *L, uint64_t guid, ByGUIDResult *out);

// Same equipment-first, bag-second walk as `FindByGUID`, but matches
// on itemID instead. Returns the FIRST instance found — equipment
// slots are checked before bags, so an equipped enchanted copy is
// preferred over an unenchanted duplicate in the backpack. Used
// when we only have the itemID for a slot (e.g. cursor-held items
// picked up from the action bar, which the engine stores as a
// bare itemID with no GUID) and want to recover the live CGItem
// for link-decoration purposes. Stomps the Lua stack.
bool FindByItemID(void *L, int itemID, ByGUIDResult *out);

// Full detail for a bag slot, for callers that drive the engine's
// cursor-pickup primitive — which needs the *container object* and the
// engine *linear slot*, not just the CGItem.
struct BagSlotDetail {
    const uint8_t *container; // PackBagSlot container (invMgr for the backpack, else the bag)
    int linearSlot;           // engine linear inventory slot
    const uint8_t *item;      // CGItem at the slot, or null if the slot is empty
};

// Resolves `(bagID, slotIndex)` to its `BagSlotDetail`. Returns false when
// the coordinates don't resolve (bad bag / out-of-range slot); a resolved
// but empty slot returns true with `out->item == nullptr`. Stomps the Lua
// stack (PackBagSlot). `ResolveBag`/`ResolveBagSlot` are thin wrappers that
// keep only `out->item`.
bool ResolveBagSlotDetail(void *L, int bagID, int slotIndex, BagSlotDetail *out);

} // namespace Item::Location

// Forward-declare for `FindByArgInBags` below.
namespace Item::Arg { struct Resolved; }

namespace Item::Location {

// The single item name/id match predicate: true if `cgItem` satisfies
// the resolved arg. `arg.itemID > 0` ⇒ exact itemID compare; otherwise
// `arg.name` is matched case-insensitively against the item's *decorated*
// display name (random suffix included — via `Item::Link::NameFromCGItem`,
// which falls back to the base name for unsuffixed items). Matches modern
// `IsEquippedItem` / by-name semantics: "Foo of the Owl" matches, the base
// "Foo" does not. Null / uncached / unnameable items → false. Shared by
// `FindByArgInBags` and `C_Item.IsEquippedItem` so the match logic lives
// in exactly one place.
bool MatchesArg(const uint8_t *cgItem, const Item::Arg::Resolved &arg);

// Walks the player's bags (0..4 only — no equipment) looking for the
// first item matching `arg` (via `MatchesArg`). On hit, populates
// `out->bagID` / `out->slotIndex` / `out->item` and sets
// `equipmentSlotIndex = 0`. Stomps the Lua stack.
//
// Used by the by-name action APIs (`C_Item.EquipItemByName`,
// `C_Item.UseItemByName`, `C_Item.UseAtCursor`) — none of them want
// equipment in the candidate pool (the engine doesn't equip-from-
// equipped or use-from-equipped; the item has to be in bags first).
bool FindByArgInBags(void *L, const Item::Arg::Resolved &arg, ByGUIDResult *out);

// Like `FindByArgInBags` but searches **equipment (1..19) first, then
// bags** — for callers that act on an item wherever it's carried (e.g.
// `C_Item.PickupItem`). On hit `out->equipmentSlotIndex` is non-zero for
// an equipped match, else `out->bagID`/`out->slotIndex` for a bag match.
// `FindByItemID` is the itemID-only specialization of this. Stomps the
// Lua stack.
bool FindByArg(void *L, const Item::Arg::Resolved &arg, ByGUIDResult *out);

// Resolves `(bagID, slotIndex)` to its `CGItem *` WITHOUT the Lua stack:
// the backpack (bag 0) is indexed straight off the player inventory manager
// at `BACKPACK_LINEAR_BASE + slot - 1`, bags 1..4 through
// `EquippedBagInventory` — both via the engine's `GetItemBySlot`. Returns
// nullptr for a bad bag, an out-of-range slot, or an empty slot. Safe from
// any context (world tick, packet hooks) — the `PackBagSlot`-based
// `ResolveBag` is not, because it reads its inputs off the Lua stack.
const uint8_t *ResolveBagSlotNoLua(int bagID, int slotIndex);

// `FindByArg` for callers with no Lua callback context: same equipment-first,
// bags-second walk and the same `MatchesArg` predicate, built on
// `ResolveEquipmentSlot` + `ResolveBagSlotNoLua`. Pure C — used by the
// `#showtooltip` evaluator on the world tick and by the action-bar display
// overrides at hover time.
bool FindByArgNoLua(const Item::Arg::Resolved &arg, ByGUIDResult *out);

// Parses the `"0xHHHHHHHHLLLLLLLL"` GUID string format
// `C_Item.GetItemGUID` returns. Strict: requires exactly `0x` prefix
// + 16 case-insensitive hex digits. Returns false (and leaves `*out`
// untouched) on malformed input.
bool ParseGUIDString(const char *s, uint64_t *out);

// Looks up a CGItem by GUID via the engine's
// `FUN_OBJECT_RESOLVE_BY_GUID`. Returns nullptr if the GUID isn't
// known to the client (item not loaded, sentinel zero/-1, etc.).
// Side-effect free — doesn't touch the Lua stack, safe outside a
// Lua callback. Same path the equipment-set walks and the merchant
// buyback chain go through.
const uint8_t *ResolveByGUID(uint64_t guid);

} // namespace Item::Location
