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

#include "item/RepairCost.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::RepairCost {

namespace {

using RepairCostFn = int (__fastcall *)(void *item);
const auto ComputeRepairCost = reinterpret_cast<RepairCostFn>(
    static_cast<uintptr_t>(Offsets::FUN_ITEM_REPAIR_COST));

} // namespace

// Pushes one int return (copper cost). 0 means "no repair needed" — the
// engine's helper returns 0 for null items, broken items, items without a
// durability concept, or fully-repaired items. We forward 0 in those cases
// rather than returning nothing, because the caller asked "what's the
// repair cost" and 0 is a meaningful answer ("nothing to repair").
//
// Exposed via [[item/RepairCost.h]] so `Container::RepairCost` can
// reuse the same engine call for `C_Container.GetContainerItemRepairCost`.
int PushRepairCostForItem(void *L, const uint8_t *item) {
    if (item == nullptr) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    const int cost = ComputeRepairCost(const_cast<uint8_t *>(item));
    Game::Lua::PushNumber(L, static_cast<double>(cost));
    return 1;
}

namespace {

// `GetInventoryItemRepairCost(invSlot)` — returns the cost in copper to
// repair the player's equipped item at `invSlot` (1-based). Matches the
// third return value of `GameTooltip:SetInventoryItem`.
//
// The faction-rep + PvP-rank discount is only applied when a merchant
// window is currently open (the engine looks up the merchant GUID from
// DAT_00BDDFA0/A4 — populated by SMSG_LIST_INVENTORY, zeroed on
// merchant frame close). Outside a vendor interaction the engine
// short-circuits the discount path and we get the raw base cost.
//
// Player-only; mirrors the shape of our existing
// `GetInventoryItemDurability`. Returns 0 for empty slots or items
// that don't need repair.
int __fastcall Script_GetInventoryItemRepairCost(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetInventoryItemRepairCost(invSlot)");
        return 0;
    }
    const int slot1Based = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (slot1Based < 1) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    return PushRepairCostForItem(L, Item::Location::ResolveEquipmentSlot(slot1Based));
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("invSlot", "luaIndex", "An equipment slot of the player, from 1 to 19."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("repairCost", "number",
                   "In copper; 0 for an empty slot or an item with nothing to repair. The "
                   "merchant discount applies only while a merchant window is open."),
};
const Game::Doc::Function kGetInventoryItemRepairCost{
    "Returns the cost to repair an item the player wears.", kArgs, kRets, "ItemGlobals", true};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetInventoryItemRepairCost",
                                      &Script_GetInventoryItemRepairCost,
                                      &kGetInventoryItemRepairCost);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::RepairCost
