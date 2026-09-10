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

// `C_Item.IsConsumableItem(item)` — true iff the item is a consumable:
// item class `Consumable` (0), or an inventory type of `INVTYPE_AMMO`
// (24) / `INVTYPE_THROWN` (25) — ammo and thrown weapons being consumed
// in use. `item` is an itemID, `"item:NNN"` string, or item link (names
// aren't resolvable in vanilla → false).
//
// Definition established by decoding 3.3.5's `Script_IsConsumableItem`
// (`0x005173E0`) and then verifying the surviving contract against a
// modern client with `/dump`: it is NOT 3.3.5's on-use-spell-effect walk
// (an on-use trinket like Talisman of Ephemeral Power returns *false*),
// but a plain class-or-ammo/thrown check. Confirmed true: Minor Healing
// Potion / Linen Bandage / Faintly Glowing Skull (all class 0), Rough
// Arrow (Ammo). Confirmed false: Hearthstone (class Misc, valueless
// teleport), on-use trinkets, plain armor.
//
// Cache-miss handling mirrors `C_Item.IsEquippableItem`: returns false
// without firing a load (sync call, matches modern semantics). Callers
// that want auto-warm behavior should run `C_Item.RequestLoadItemDataByID`
// first and re-check on `ITEM_DATA_LOAD_RESULT`.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Record.h"

#include <cstdint>

namespace Item::Consumable {

namespace {

constexpr uint32_t kItemClassConsumable = 0;

int __fastcall Script_C_Item_IsConsumableItem(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const uint32_t classID = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t invType = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);

    const bool consumable = classID == kItemClassConsumable ||
                            invType == Offsets::INVTYPE_AMMO ||
                            invType == Offsets::INVTYPE_THROWN;
    Game::Lua::PushBoolean(L, consumable);
    return 1;
}

void RegisterLuaFunctions() {
    // Modern exposes both the namespaced `C_Item.IsConsumableItem` and the
    // bare global `IsConsumableItem` (the older, still-live form); register
    // both against the same handler, like `IsUsableItem`.
    Game::Lua::RegisterTableFunction("C_Item", "IsConsumableItem",
                                     &Script_C_Item_IsConsumableItem);
    Game::Lua::RegisterGlobalFunction("IsConsumableItem",
                                      &Script_C_Item_IsConsumableItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::Consumable
