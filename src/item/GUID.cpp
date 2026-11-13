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
#include "guid/Guid.h"
#include "item/CGItem.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::GUID {

namespace {

// Reads a CGItem's per-instance 64-bit GUID (instance block at +0x08 →
// GUID qword at +0x00). Returns 0 if the item or block is null.
uint64_t ReadItemGuid(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    auto *instance = Item::InstanceBlock(cgItem);
    if (instance == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_GUID);
}

} // namespace

// `C_Item.GetItemGUID(itemLocation)` — returns the per-instance 64-bit
// GUID of the item at the location formatted as `"0xHHHHHHHHLLLLLLLL"`
// (16 hex digits, hi dword first). Same format `UnitGUID` returns —
// 1.12 GUIDs are plain qwords with no `"Item-Server-..."`-style prefix
// scheme (that's 6.x+). The format is stable per-character-session
// and survives bag moves, so addons can use it as a slot-independent
// identifier for a specific item instance.
//
// Reads CGItem instance block at `+0x08` → GUID qword at `+0x00`. No
// cache lookup; works for any inventory or equipment item.
//
// Returns nil if the location is invalid, empty, or the instance block
// is somehow missing (shouldn't happen for live items).
static int __fastcall Script_C_Item_GetItemGUID(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.GetItemGUID(itemLocation)");
        return 0;
    }
    const uint8_t *item = Item::Location::Resolve(L, 1);
    if (item == nullptr)
        return 0;
    const uint64_t guid = ReadItemGuid(item);
    if (guid == 0)
        return 0;

    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(guid, buf, sizeof buf));
    return 1;
}

// `C_Item.IsItemGUIDInInventory(itemGUID)` — returns whether an item with
// the given `"0x…"` GUID is currently carried by the player: the 19
// equipment slots plus the bags (backpack + the four equipped bags,
// containers 0..4). Equipped items count — verified against retail, where
// `IsItemGUIDInInventory(GetItemGUID({equipmentSlotIndex=1}))` is `true`.
// The bank is NOT included (separate storage; `GetItemCount(..., true)`
// covers it). Accepts the same GUID string `C_Item.GetItemGUID` /
// `UnitGUID` return. Returns `false` for a malformed / zero GUID.
static int __fastcall Script_C_Item_IsItemGUIDInInventory(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Item.IsItemGUIDInInventory(\"itemGUID\")");
        return 0;
    }
    // Parse into a plain qword BEFORE any ResolveBag call — ResolveBag
    // clobbers the Lua stack (and thus the argument string) as it packs
    // bag/slot into stack[1]/[2]. (ResolveEquipmentSlot uses the inventory
    // manager directly and doesn't touch the stack.)
    uint64_t target = 0;
    if (!Guid::Parse(Game::Lua::ToString(L, 1), &target) || target == 0) {
        Game::Lua::SetTop(L, 0);
        Game::Lua::PushBool(L, false);
        return 1;
    }

    bool found = false;
    // Equipped slots 1..19.
    for (int slot = 1; slot <= 19 && !found; ++slot) {
        const uint8_t *item = Item::Location::ResolveEquipmentSlot(slot);
        if (item != nullptr && ReadItemGuid(item) == target)
            found = true;
    }
    // Bags 0..4 (backpack + equipped bags).
    for (int bag = 0; bag <= 4 && !found; ++bag) {
        const int slots = Item::Location::GetBagSlotCount(bag);
        for (int slot = 1; slot <= slots; ++slot) {
            const uint8_t *item = Item::Location::ResolveBag(L, bag, slot);
            if (item != nullptr && ReadItemGuid(item) == target) {
                found = true;
                break;
            }
        }
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBool(L, found);
    return 1;
}

static const Game::Doc::Field kGetItemGUIDArgs[] = {
    Game::Doc::Req("itemLocation", "ItemLocation", "An item location or an item GUID."),
};
static const Game::Doc::Field kGetItemGUIDRets[] = {
    Game::Doc::Opt("itemGUID", "WOWGUID", nullptr,
                   "Nil for an empty slot or a location that is not valid."),
};
static const Game::Doc::Function kGetItemGUID{
    "Returns the GUID of the item at a location, which stays the same when the item moves.",
    kGetItemGUIDArgs, kGetItemGUIDRets};

static const Game::Doc::Field kIsItemGUIDInInventoryArgs[] = {
    Game::Doc::Req("itemGUID", "WOWGUID"),
};
static const Game::Doc::Field kIsItemGUIDInInventoryRets[] = {
    Game::Doc::Req("valid", "bool", "False also for a GUID that is not valid."),
};
static const Game::Doc::Function kIsItemGUIDInInventory{
    "Returns whether the player wears the item or carries it in the bags; the bank does not count.",
    kIsItemGUIDInInventoryArgs, kIsItemGUIDInInventoryRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemGUID", &Script_C_Item_GetItemGUID,
                                     &kGetItemGUID);
    Game::Lua::RegisterTableFunction("C_Item", "IsItemGUIDInInventory",
                                     &Script_C_Item_IsItemGUIDInInventory,
                                     &kIsItemGUIDInInventory);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::GUID
