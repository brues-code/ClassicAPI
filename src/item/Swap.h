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

namespace Item::Swap {

// Sends a server-side swap to equip `cgItem` into the given paperdoll
// slot, by way of the engine's own packet builder/sender at
// FUN_INVENTORY_SWAP (0x005E0C40). Same code path drag-and-drop equip
// uses — the cursor is never read or written.
//
// Variants differ only in how they encode the *source* location:
//
// - `FromBag(item, bagID, slotInBag, dst)` — item currently in
//   container `bagID` at 1-based `slotInBag`. `bagID = 0` is the
//   backpack (linear-slot 22 + slotInBag, container = player);
//   `bagID = 1..4` is an equipped bag (linear-slot `slotInBag - 1`,
//   container = that bag's GUID).
//
// - `FromPaperdoll(item, srcSlot, dst)` — item currently in
//   paperdoll slot `srcSlot` (1-based). Used for paperdoll-to-
//   paperdoll swaps like rings 11/12, trinkets 13/14.
//
// - `ToBag(item, srcPaperdollSlot, dstBag, dstSlot)` — item currently
//   in paperdoll slot, moving to a bag slot. The "unequip" direction
//   `UseEquipmentSet` needs for slots the set wants empty. Uses the
//   same atomic swap packet — server-side accept rules are identical
//   (bagID 0..4, dst slot must be valid for that bag).
//
// All slot args are 1-based Lua-facing; helpers do the
// linear-slot conversion. Returns `false` on bad args (slot out of
// range, no player object, missing instance block on the item or
// player), `true` after dispatching the packet (does NOT wait for
// server confirmation).
//
// Caller must already hold the source CGItem pointer — typically
// obtained from the location walk that discovered the swap target.
bool FromBag(const void *cgItem, int bagID, int slotInBag, int dstPaperdollSlot);

bool FromPaperdoll(const void *cgItem, int srcPaperdollSlot, int dstPaperdollSlot);

bool ToBag(const void *cgItem, int srcPaperdollSlot, int dstBagID, int dstSlotInBag);

// General bag-to-bag swap — neither side is constrained to the
// paperdoll. Same engine helper (`FUN_INVENTORY_SWAP`), which routes
// to opcode 0x10D (same-container slot swap, e.g. backpack ↔ backpack)
// or 0x10C (cross-container swap, e.g. backpack → equipped bag) based
// on whether the two container GUIDs match.
//
// bagID convention matches `C_Container.GetContainerItemInfo`:
//   0    = backpack
//   1..4 = equipped bags (in equipment slots 20..23)
// slot is 1-based. Returns false if either bag is unequipped, either
// slot is out of bounds, or the source slot is empty. Destination is
// allowed to be empty — the engine treats that as a move; or occupied
// — engine atomically swaps.
//
// Requires a valid Lua state pointer because the source-side CGItem
// lookup goes through `Item::Location::ResolveBag`, which uses the
// engine's `PackBagSlot` helper. Stomps the Lua stack — callers must
// read every arg they need before calling.
bool Containers(void *L, int srcBag, int srcSlot, int dstBag, int dstSlot);

// Atomic server-side "split N items from src and place at dst" — same
// wire-level primitive vanilla emits when you `SplitContainerItem` →
// drop-on-target, but bundled into one packet (`CMSG_SPLIT_ITEM`,
// opcode 0x10E). No cursor involvement.
//
// Semantics (server-enforced, all-or-nothing):
//   - `dst` empty                        → places `count` there
//   - `dst` same item & fits maxStack    → merges `count` into dst
//   - `dst` different item / overflow    → server rejects (no partial)
//
// Source must be occupied with at least `count` items. Locally
// validates `count <= srcStack` (server would reject otherwise with
// `EQUIP_ERR_COULDNT_SPLIT_ITEMS` — fail fast). When `count ==
// srcStack` ("move the whole stack"), routes through `Containers`
// instead — vanilla's `CMSG_SPLIT_ITEM` rejects splits that would
// leave the source empty, but `CMSG_SWAP_ITEM` handles the case
// (merges into a matching destination stack, swaps otherwise).
// Returns false locally on bad args, unresolvable bags, or
// `count > srcStack`; all other validation is server-side.
//
// Requires Lua state for the same reason as `Containers`: the source
// CGItem lookup goes through `Item::Location::ResolveBag`. Stomps the
// Lua stack.
bool MoveCount(void *L, int srcBag, int srcSlot, int dstBag, int dstSlot, int count);

// "Put this item away" — the server picks the destination slot
// (`CMSG_AUTOSTORE_BAG_ITEM`, opcode 0x10B). Unlike `Containers` and
// `MoveCount` there is no destination slot to supply, and that is the
// whole value of it: the server first merges the stack into existing
// non-full stacks of the same item (splitting it across several if
// needed), then places any remainder in a free slot.
//
// So this is the right primitive for consolidating partial stacks,
// because it needs no stack-size lookup on this side. Computing the
// merge here would mean sizing every stack first, and the max-stack
// getter is nil until that item's data has arrived from the server —
// so a cold item cache silently skips exactly the stacks it could not
// size. The server never has to look one up.
//
// `dstBag` says where you want it, and two different engine builders
// serve it because the wire has two autostore opcodes:
//   dstBag 0    → anywhere in the main inventory that it fits
//   dstBag 1..4 → that equipped bag only
//   dstBag -1   → anywhere in the bank that it fits
//
// The backpack is not separately addressable, because on the wire the
// backpack IS the player container — so 0 means the whole inventory,
// not "the backpack". Individual bank bags (5..10) are likewise not
// addressable: the bank opcode carries no destination at all.
//
// WHAT IS NOT EXPRESSIBLE: moving an item to a fresh slot on the side
// it is already on, when that side is the bank. The bank opcode derives
// its direction from the source, so a bank-sourced item always travels
// to the inventory. Consolidating stacks WITHIN the bank therefore
// cannot use this call — use `MoveCount` per pair instead. The
// combinations this accepts are exactly:
//   inventory source, dstBag 0..4  → stays in the inventory
//   inventory source, dstBag -1    → goes to the bank
//   bank source,      dstBag 0     → comes back to the inventory
// Anything else returns false rather than sending a packet that would
// do something other than what was asked.
//
// Returns false on bad args, an unequipped bag, a rejected
// source/destination pair, or an empty source slot. Everything else is
// server-side; failures come back as `SMSG_INVENTORY_CHANGE_FAILURE`
// exactly as for the other two. Stomps the Lua stack for the same
// reason `Containers` does.
bool AutoStore(void *L, int srcBag, int srcSlot, int dstBag);

// Lua-free forms of `Containers` and `AutoStore`, for callers that
// already hold the source `CGItem *` and therefore need no lookup.
//
// The Lua-taking forms exist only to resolve that pointer, and they do
// it through `Item::Location::ResolveBag` → the engine's `PackBagSlot`,
// which reads its arguments off the Lua stack and stomps it. That is
// fine inside a Lua call, where the stack is ours. It is NOT fine from
// a tick or bag-update callback, where the engine may have its own
// values there — so anything running outside a Lua call must use these.
//
// The caller owns the pointer's validity. It stays valid across a batch
// of swaps: a server-side swap moves item GUIDs between slots and does
// not destroy the objects, so pointers snapshotted before the batch are
// still good during it.
bool ContainersFrom(const void *srcItem, int srcBag, int srcSlot,
                    int dstBag, int dstSlot);

bool AutoStoreFrom(const void *srcItem, int srcBag, int srcSlot, int dstBag);

// CAUTION when batching. This reads the source's LIVE stack count to
// decide whether `count` is the whole stack (which must go as a swap,
// since the server rejects a split that would empty its source) or a
// partial one. A caller firing several moves in one frame gets no server
// replies in between, so those counts are still the pre-batch values
// while the caller's own model has moved on — and a move the caller
// knows is "the last 3" is sent as a split of 3 from a stack the server
// says holds 3, which it refuses with "Couldn't split those items".
// Such a caller should decide from its own model and call
// `ContainersFrom` directly for whole-stack moves.
bool MoveCountFrom(const void *srcItem, int srcBag, int srcSlot, int dstBag,
                   int dstSlot, int count);

} // namespace Item::Swap
