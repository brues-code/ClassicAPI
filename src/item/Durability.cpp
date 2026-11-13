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

#include "item/Durability.h"

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::Durability {

// Reads ITEM_FIELD_DURABILITY (+0xA0) and ITEM_FIELD_MAXDURABILITY (+0xA4)
// off the CGItem's m_objectFields descriptor at `+0x114`. Same path
// `Script_GetInventoryItemBroken` (`0x004C8590`) uses; field offsets
// verified there. Returns false (no values pushed) when the item is
// missing, the descriptor is null, or max is 0 (item has no durability
// concept — consumables, materials, rings, trinkets, etc.). Modern
// semantics: nothing rather than (0, 0).
//
// Exposed via [[item/Durability.h]] so `Container::Durability` can
// reuse the same descriptor read for `C_Container.GetContainerItemDurability`.
int PushDurabilityForItem(void *L, const uint8_t *item) {
    if (item == nullptr)
        return 0;
    auto *descriptor = Item::ObjectFields(item);
    if (descriptor == nullptr)
        return 0;
    const uint32_t cur = *reinterpret_cast<const uint32_t *>(
        descriptor + Offsets::OFF_DESCRIPTOR_DURABILITY);
    const uint32_t max = *reinterpret_cast<const uint32_t *>(
        descriptor + Offsets::OFF_DESCRIPTOR_MAX_DURABILITY);
    if (max == 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(cur));
    Game::Lua::PushNumber(L, static_cast<double>(max));
    return 2;
}

namespace {

// `GetInventoryItemDurability(invSlot)` — returns `current, maximum` for
// the player's equipped item at `invSlot` (1-based), or nothing if the
// slot is empty or the item has no durability. Player-only by design,
// matching modern API; 3.3.5's implementation at `0x005EA170` does the
// same (uses `ResolveActivePlayer`, no unit arg).
int __fastcall Script_GetInventoryItemDurability(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetInventoryItemDurability(invSlot)");
        return 0;
    }
    const int slot1Based = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (slot1Based < 1)
        return 0;
    return PushDurabilityForItem(L, Item::Location::ResolveEquipmentSlot(slot1Based));
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("invSlot", "luaIndex", "An equipment slot of the player, from 1 to 19."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("current", "number", nullptr,
                   "Nil when the slot is empty or the item has no durability; 0 when broken."),
    Game::Doc::Opt("maximum", "number"),
};
const Game::Doc::Function kGetInventoryItemDurability{
    "Returns the current and maximum durability of an item the player wears.", kArgs, kRets,
    "ItemGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetInventoryItemDurability",
                                      &Script_GetInventoryItemDurability,
                                      &kGetInventoryItemDurability);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Durability
