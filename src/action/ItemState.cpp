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
#include "action/ExtendedState.h"
#include "item/Cooldown.h"
#include "macro/ShowTooltip.h"

#include <cstdint>

namespace Action::ItemState {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
using ItemCount_t = uint32_t(__fastcall *)(uint32_t itemID);

int Engine(uintptr_t fn, void *L) {
    return reinterpret_cast<ScriptFn_t>(fn)(L);
}

// True (and fills `info`) when stack[1] is one of the engine's slots whose
// macro resolved to an item. These globals answer 1..120 only; slots past 120
// are C_ActionBar's.
bool ItemTarget(void *L, Macro::ShowTooltip::Info *info) {
    if (!Game::Lua::IsNumber(L, 1))
        return false;
    const int slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    return slot0 >= 0 && slot0 < Offsets::ACTION_TABLE_MAX_SLOTS &&
           Macro::ShowTooltip::ForSlot(slot0, info) &&
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

int __fastcall Script_IsConsumableAction(void *L) {
    Macro::ShowTooltip::Info info;
    if (!ItemTarget(L, &info))
        return Engine(Offsets::FUN_SCRIPT_IS_CONSUMABLE_ACTION, L);
    Game::Lua::PushFlag(
        L, Action::ExtendedState::IsConsumableItem(static_cast<uint32_t>(info.itemID)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "The action slot, 1 to 120."),
};

const Game::Doc::Field kCooldownRets[] = {
    Game::Doc::Req("startTime", "number", "In GetTime() seconds; 0 when there is no cooldown."),
    Game::Doc::Req("duration", "number", "In seconds; 0 when there is no cooldown."),
    Game::Doc::Req("enable", "number",
                   "0 while the cooldown waits to start, such as for a potion used in combat."),
};
const Game::Doc::Function kGetActionCooldown{
    "Returns the cooldown of an action, also for a macro that shows an item.",
    kSlotArgs, kCooldownRets, "ActionBarGlobals"};

const Game::Doc::Field kCountRets[] = {
    Game::Doc::Req("count", "number"),
};
const Game::Doc::Function kGetActionCount{
    "Returns how many of an action's item or reagent you carry, also for a macro that shows "
    "an item.",
    kSlotArgs, kCountRets, "ActionBarGlobals"};

const Game::Doc::Field kConsumableRets[] = {
    Game::Doc::Opt("isConsumable", "number", nullptr,
                   "1 when the action uses up an item, else nil."),
};
const Game::Doc::Function kIsConsumableAction{
    "Returns whether an action uses up an item, also for a macro that shows an item.",
    kSlotArgs, kConsumableRets, "ActionBarGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetActionCooldown", &Script_GetActionCooldown,
                                      &kGetActionCooldown);
    Game::Lua::RegisterGlobalFunction("GetActionCount", &Script_GetActionCount,
                                      &kGetActionCount);
    Game::Lua::RegisterGlobalFunction("IsConsumableAction", &Script_IsConsumableAction,
                                      &kIsConsumableAction);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Action::ItemState
