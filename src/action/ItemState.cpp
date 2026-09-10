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

// `GetActionCooldown` / `GetActionCount` / `IsConsumableAction` for macro
// slots whose `#showtooltip` / `#show` resolved to an ITEM. A spell
// resolution needs none of this — the engine reads a macro slot's spell from
// its primary-spell cache, which `Macro::ShowTooltip` writes — but the engine
// has no per-macro item field, so the item forms of these three are answered
// here, each with the engine's own helper for the item-by-ID slot case:
//
//   - cooldown  → `FUN_ITEM_QUERY_COOLDOWN` via `Item::Cooldown::PushCooldown`
//                 (the same triple `GetContainerItemCooldown` returns);
//   - count     → `FUN_ACTION_ITEM_COUNT`, the value `UseAction` caches per
//                 item-by-ID slot;
//   - consumable → the rule of the inner `IsConsumableAction` (`0x004E5250`):
//                 ammo / thrown, or any on-use spell slot with negative
//                 charges.
//
// Registered in front of the engine globals (last registration wins) and a
// strict superset of them: every other slot tail-calls the engine function
// with the untouched stack.

#include "Game.h"
#include "Offsets.h"
#include "item/Cooldown.h"
#include "item/Record.h"
#include "macro/ShowTooltip.h"

#include <cstdint>

namespace Action::ItemState {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using ItemCount_t = uint32_t(__fastcall *)(uint32_t itemID);

// ItemStats `inventoryType` values the engine's consumable rule tests.
constexpr uint32_t INVTYPE_AMMO = 24;
constexpr uint32_t INVTYPE_THROWN = 25;
constexpr int kItemSpellSlots = 5;

int Engine(uintptr_t fn, void *L) {
    return reinterpret_cast<ScriptFn_t>(fn)(L);
}

// True (and fills `info`) when stack[1] is a slot whose macro resolved to an item.
bool ItemTarget(void *L, Macro::ShowTooltip::Info *info) {
    if (!Game::Lua::IsNumber(L, 1))
        return false;
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    return Macro::ShowTooltip::ForSlot(slot0, info) &&
           info->target == Macro::ShowTooltip::Target::Item;
}

int __fastcall Script_GetActionCooldown(void *L) {
    Macro::ShowTooltip::Info info;
    if (!ItemTarget(L, &info))
        return Engine(Offsets::FUN_SCRIPT_GET_ACTION_COOLDOWN, L);
    Item::Cooldown::PushCooldown(L, info.itemID);
    return 3;
}

int __fastcall Script_GetActionCount(void *L) {
    Macro::ShowTooltip::Info info;
    if (!ItemTarget(L, &info))
        return Engine(Offsets::FUN_SCRIPT_GET_ACTION_COUNT, L);
    const uint32_t count = reinterpret_cast<ItemCount_t>(Offsets::FUN_ACTION_ITEM_COUNT)(
        static_cast<uint32_t>(info.itemID));
    Game::Lua::PushNumber(L, static_cast<double>(count));
    return 1;
}

bool IsConsumable(int itemID) {
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return false;
    const uint32_t invType = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    if (invType == INVTYPE_AMMO || invType == INVTYPE_THROWN)
        return true;
    for (int i = 0; i < kItemSpellSlots; ++i) {
        const uint32_t spellID = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4);
        const uint32_t trigger = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + i * 4);
        const int32_t charges = Game::Read<int32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_CHARGES + i * 4);
        if (spellID != 0 && trigger == 0 && charges < 0)
            return true;
    }
    return false;
}

int __fastcall Script_IsConsumableAction(void *L) {
    Macro::ShowTooltip::Info info;
    if (!ItemTarget(L, &info))
        return Engine(Offsets::FUN_SCRIPT_IS_CONSUMABLE_ACTION, L);
    if (IsConsumable(info.itemID))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetActionCooldown", &Script_GetActionCooldown);
    Game::Lua::RegisterGlobalFunction("GetActionCount", &Script_GetActionCount);
    Game::Lua::RegisterGlobalFunction("IsConsumableAction", &Script_IsConsumableAction);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Action::ItemState
