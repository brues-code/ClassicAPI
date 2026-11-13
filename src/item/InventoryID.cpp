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
#include "item/CGItem.h"
#include "unit/Flags.h"

#include <cstdint>

namespace Item::InventoryID {

namespace {

using GetItemBySlot_t = void *(__thiscall *)(void *invMgr, int slot);
using GetVisibleItem_t = void *(__thiscall *)(void *unit, int slot);

void *ResolveUnit(const char *token) {
    return Game::ResolveUnitToken(token);
}

// Walks the local player's private inventory manager — the same path
// `C_Item.GetItemID({equipmentSlotIndex=N})` uses. Bag/bank slots also
// resolve here; we don't bound the slot up front since the engine's
// own `GetItemBySlot` handles invalid slots by returning NULL.
int ItemIDForLocalPlayer(void *playerPtr, int slot1Based) {
    auto *player = static_cast<uint8_t *>(playerPtr);
    auto *invMgr = player + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
    auto getBySlot = reinterpret_cast<GetItemBySlot_t>(Offsets::FUN_ITEMMGR_GET_ITEM_BY_SLOT);
    auto *item = static_cast<uint8_t *>(getBySlot(invMgr, slot1Based - 1));
    if (item == nullptr)
        return 0;
    auto *instance = Item::InstanceBlock(item);
    if (instance == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID));
}

// For non-local-player units, the engine maintains a "visible items"
// array on the CGUnit fed by the broadcast UpdateField range. Equipment
// slots 1..19 (1-based) read directly from `[unit + 0xE68] + 0x118 +
// slot*0x30`. Bag/bank slots aren't broadcast, so they're not
// addressable for non-local units — the helper returns NULL for slots
// outside [0, 18] (0-based).
//
// Caller must have already gated on `Unit::Flags::IsPlayerControlled`
// — the engine helper assumes a valid visible-items array and crashes
// otherwise.
int ItemIDForOtherUnit(void *unitPtr, int slot1Based) {
    auto getVisibleItem = reinterpret_cast<GetVisibleItem_t>(
        Offsets::FUN_UNIT_GET_VISIBLE_ITEM);
    auto *entry = static_cast<uint8_t *>(getVisibleItem(unitPtr, slot1Based - 1));
    if (entry == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_VISIBLE_ITEM_ITEM_ID));
}

} // namespace

// `GetInventoryItemID(unit, slot)` — same arg shape as 1.12's
// `GetInventoryItemLink(unit, slot)`, but returns the itemID directly
// instead of a hyperlink string. Useful when an addon already knows
// it'll only ever feed the result back to itemID-keyed APIs
// (`GetItemInfoInstant`, the item cache, etc.) and parsing the link
// is just churn.
//
// Path selection mirrors the engine's own approach in
// `Script_GetInventoryItemLink`:
//   - resolved unit pointer == "player" pointer  → private invManager
//     (always populated, supports bag/bank slots).
//   - any other unit  → visible-items array via the engine helper at
//     `FUN_UNIT_GET_VISIBLE_ITEM` (only equipment slots 1..19).
// The engine compares GUIDs there; we compare CGUnit pointers, which
// works because `ResolveUnitToken` returns the same cached
// per-entity pointer regardless of which alias ("target", "focus" if
// it existed, etc.) the caller used.
static int __fastcall Script_GetInventoryItemID(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GetInventoryItemID(unit, slot)");
        return 0;
    }
    const char *unitToken = Game::Lua::ToString(L, 1);
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (unitToken == nullptr || slot < 1)
        return 0;

    void *unitPtr = ResolveUnit(unitToken);
    if (unitPtr == nullptr)
        return 0;

    void *playerPtr = ResolveUnit("player");
    int itemID = 0;
    if (unitPtr == playerPtr) {
        itemID = ItemIDForLocalPlayer(playerPtr, slot);
    } else if (Unit::Flags::IsPlayerObject(
                   static_cast<const uint8_t *>(unitPtr))) {
        itemID = ItemIDForOtherUnit(unitPtr, slot);
    } else {
        // Non-player object — no CGPlayer visible-items array at +0xE68.
        // Gate on the object being a player, not merely player-controlled:
        // a pet/totem/MC'd creature sets UNIT_FLAG_PLAYER_CONTROLLED but
        // has no sub-struct there, so IsPlayerControlled would let it
        // through and `ItemIDForOtherUnit` would deref garbage and crash
        // (same root cause as pfUI issue #34). Returning nil also matches
        // `GetInventoryItemLink`'s practical behavior on non-players.
        return 0;
    }
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

static const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
    Game::Doc::Req("slot", "luaIndex",
                   "An inventory slot; for a unit other than the player, only slots 1 to 19."),
};
static const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil for an empty slot, or a unit that does not exist or is not a player."),
};
static const Game::Doc::Function kGetInventoryItemID{
    "Returns the item ID of the item in a unit's inventory slot.", kArgs, kRets, "ItemGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetInventoryItemID", &Script_GetInventoryItemID,
                                      &kGetInventoryItemID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::InventoryID
