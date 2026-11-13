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

// `C_Item.IsLocked` / `C_Item.UnlockItem` / `C_Item.UnlockAllItems` —
// client-side "in-transaction" lock state. The lock is a per-CGItem
// instance flag at `OFF_ITEM_CLIENT_LOCK` bit 0, set optimistically
// by the engine's pickup/equip paths *before* the transaction packet
// is sent, and cleared by the SMSG_UPDATE_OBJECT post-processor once
// the server confirms.
//
// The engine emits no Lua wrapper for set / clear — `C_Item.LockItem`
// doesn't exist (and shouldn't: setting the lock without a real
// pending transaction desyncs the UI from the server). Vanilla's
// `ITEM_LOCK_CHANGED` event (engine ID 0xBC = 188) fires on every
// change; addons that just want notification can listen there.
//
// `UnlockItem` / `UnlockAllItems` exist for stuck-lock recovery —
// when a transaction packet's server confirmation never arrives, the
// affected item stays visually greyed until logout (the only engine
// trigger for the unlock-all sweep is `PLAYER_LEAVING_WORLD`).

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "item/CGItem.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::Lock {

namespace {

// Both setter and clearer share the `__stdcall(uint guidLo, int guidHi)` shape —
// they end with `RET 0x8` (callee cleans the two stack args). Declaring these
// as `__cdecl` was a calling-convention mismatch that corrupted ESP by 8 bytes
// per call; stack damage accumulated until the next Lua dispatch read a
// smashed function pointer and crashed. The sweep helper (`UnlockAll_t`) has
// no args, so its `RET 0` is convention-neutral.
using LockByGuid_t = void(__stdcall *)(uint32_t guidLo, uint32_t guidHi);

// Engine's sweep — no args, convention-neutral.
using UnlockAll_t = void(__cdecl *)();

bool ReadItemGuid(const uint8_t *item, uint32_t *lo, uint32_t *hi) {
    if (item == nullptr)
        return false;
    auto *inst = Item::InstanceBlock(item);
    if (inst == nullptr)
        return false;
    *lo = Game::Read<uint32_t>(inst, Offsets::OFF_INSTANCE_BLOCK_GUID);
    *hi = Game::Read<uint32_t>(inst, Offsets::OFF_INSTANCE_BLOCK_GUID + 4);
    return true;
}

// `C_Item.IsLocked(itemLocation)` — true iff the item is in the
// "transaction-in-progress" lock state. Reads `[item+0x314] & 1` —
// the same bit `Script_PickupContainerItem` and friends OR in via
// `FUN_004953E0` after building the cursor state, and the bit the
// SMSG_UPDATE_OBJECT post-processor clears via `FUN_00495420` once
// the server confirms.
//
// Earlier versions of this file read `descriptor[+0x3C] & 0x4`
// instead (claiming bit 2 of ITEM_FIELD_FLAGS was the lock). That
// was borrowed from modern WoW's encoding; vanilla 1.12's actual
// lock lives at the instance offset, not in m_objectFields.
int __fastcall Script_C_Item_IsLocked(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uint8_t *item = Item::Location::Resolve(L, 1);
    if (item == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uint32_t flags = Game::Read<uint32_t>(
        item, Offsets::OFF_ITEM_CLIENT_LOCK);
    Game::Lua::PushBool(L, (flags & Offsets::ITEM_CLIENT_LOCK_BIT) != 0);
    return 1;
}

// `C_Item.UnlockItem(itemLocation)` — force-clear the client-side
// lock on a single item. Calls the engine's own
// `FUN_ITEM_UNLOCK_BY_GUID`, which writes `[item+0x314] &= ~1` and
// fires `ITEM_LOCK_CHANGED`. Same primitive the SMSG_UPDATE_OBJECT
// post-processor uses when the server confirms a transaction; we
// expose it for recovery when that server confirmation never arrives.
//
// Does NOT cancel any pending cursor / mail / trade transaction —
// the server doesn't know we unlocked. If the item is genuinely
// mid-transaction the lock will reappear once the server's next
// update arrives. Pair with `ClearCursor()` (vanilla-native) for
// the cursor case.
//
// No return value (the caller has nothing meaningful to do with
// success vs. miss).
int __fastcall Script_C_Item_UnlockItem(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.UnlockItem(itemLocation)");
        return 0;
    }
    const uint8_t *item = Item::Location::Resolve(L, 1);
    uint32_t lo = 0, hi = 0;
    if (!ReadItemGuid(item, &lo, &hi))
        return 0;
    auto fn = reinterpret_cast<LockByGuid_t>(
        static_cast<uintptr_t>(Offsets::FUN_ITEM_UNLOCK_BY_GUID));
    fn(lo, hi);
    return 0;
}

// `C_Item.LockItem(itemLocation)` — set the client-side lock on a
// single item by location. Calls `FUN_ITEM_LOCK_BY_GUID` —
// `[item+0x314] |= 1` and fires `ITEM_LOCK_CHANGED`. Same primitive
// the engine's pickup/equip paths use optimistically before sending
// the transaction packet.
//
// Use cases are narrow: addon code that wants to flag items as
// "being processed" for the player's UI, without actually issuing a
// transaction. **Setting this without a real pending server-side
// transaction desyncs the lock from any actual engine state** — the
// item will appear locked indefinitely until you call `UnlockItem`
// or `UnlockAllItems` (or the next SMSG_UPDATE_OBJECT for that item
// arrives, which clears it). Treat as a UI hint, not real
// transaction state.
int __fastcall Script_C_Item_LockItem(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.LockItem(itemLocation)");
        return 0;
    }
    const uint8_t *item = Item::Location::Resolve(L, 1);
    uint32_t lo = 0, hi = 0;
    if (!ReadItemGuid(item, &lo, &hi))
        return 0;
    auto fn = reinterpret_cast<LockByGuid_t>(
        static_cast<uintptr_t>(Offsets::FUN_ITEM_LOCK_BY_GUID));
    fn(lo, hi);
    return 0;
}

// `C_Item.LockItemByGUID(itemGUID)` — set the client-side lock by
// GUID string (the `"0xHHHHHHHHLLLLLLLL"` format `C_Item.GetItemGUID`
// returns). Reaches a wider set of items than the location-based
// `LockItem`: the engine helper resolves through the object manager,
// so it can mark items the player doesn't currently have in
// bags/equipment (trade window contents, mail attachments, freshly
// looted but not yet in inventory, etc.) as long as the CGItem is
// resident.
//
// Same caveat as `LockItem` — purely a client-side visual flag,
// doesn't reflect any real engine state.
int __fastcall Script_C_Item_LockItemByGUID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.LockItemByGUID(itemGUID)");
        return 0;
    }
    uint64_t guid = 0;
    if (!Guid::Parse(Game::Lua::ToString(L, 1), &guid) || guid == 0)
        return 0;
    auto fn = reinterpret_cast<LockByGuid_t>(
        static_cast<uintptr_t>(Offsets::FUN_ITEM_LOCK_BY_GUID));
    fn(static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
    return 0;
}

// `C_Item.UnlockAllItems()` — sweep clear of every CGItem the
// engine knows about (bag containers + their contents). Single
// `ITEM_LOCK_CHANGED` fires at the end of the walk.
//
// Engine itself only triggers the sweep on PLAYER_LEAVING_WORLD —
// we expose it so addons don't need a relog to recover from stuck
// locks. Same cursor-state caveat as `UnlockItem` above: server
// doesn't know we did this.
int __fastcall Script_C_Item_UnlockAllItems(void *L) {
    (void)L;
    auto fn = reinterpret_cast<UnlockAll_t>(
        static_cast<uintptr_t>(Offsets::FUN_ITEM_UNLOCK_ALL));
    fn();
    return 0;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kLocationArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
const Game::Doc::Field kIsLockedRets[] = {
    Game::Doc::Req("isLocked", "bool",
                   "False for an empty slot or a location that names no item."),
};
const Game::Doc::Function kIsLocked{
    "Returns whether the item is locked while a pickup, trade, mail or swap of it is in progress.",
    kLocationArgs, kIsLockedRets};

const Game::Doc::Function kLockItem{
    "Locks the item at a location on this client only, and fires ITEM_LOCK_CHANGED.",
    kLocationArgs, {}};

const Game::Doc::Field kLockItemByGUIDArgs[] = {
    Game::Doc::Req("itemGUID", "WOWGUID",
                   "Any item the client has loaded, also one outside your bags."),
};
const Game::Doc::Function kLockItemByGUID{
    "Locks the item with a GUID on this client only, and fires ITEM_LOCK_CHANGED.",
    kLockItemByGUIDArgs, {}};

const Game::Doc::Function kUnlockItem{
    "Clears the lock on the item at a location on this client only, and fires "
    "ITEM_LOCK_CHANGED.",
    kLocationArgs, {}};

const Game::Doc::Function kUnlockAllItems{
    "Clears the lock on every item the client has loaded, and fires ITEM_LOCK_CHANGED once.",
    {}, {}, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "IsLocked", &Script_C_Item_IsLocked, &kIsLocked);
    Game::Lua::RegisterTableFunction("C_Item", "LockItem", &Script_C_Item_LockItem, &kLockItem);
    Game::Lua::RegisterTableFunction("C_Item", "LockItemByGUID",
                                     &Script_C_Item_LockItemByGUID, &kLockItemByGUID);
    Game::Lua::RegisterTableFunction("C_Item", "UnlockItem", &Script_C_Item_UnlockItem,
                                     &kUnlockItem);
    Game::Lua::RegisterTableFunction("C_Item", "UnlockAllItems",
                                     &Script_C_Item_UnlockAllItems, &kUnlockAllItems);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::Lock
