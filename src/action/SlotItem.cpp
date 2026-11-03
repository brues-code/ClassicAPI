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

// The item behind an action slot, 4.3.4's CGActionBar::GetItem: an item-by-ID
// action's item, or the item a macro's `#showtooltip` / `#show` resolved to
// (1.12 keeps no item per macro, so `Macro::ShowTooltip` holds it). Both
// readers below peek the item cache and never query it, as 4.3.4 does.
//
//   IsStackableAction            the item's max stack is more than 1 —
//                                4.3.4's IsStackableAucItem. The global
//                                answers 1/nil, the C_ActionBar form a bool.
//   GetItemActionOnEquipSpellID  the item's first on-equip spell (trigger 1),
//                                nil when it has none.

#include "Game.h"
#include "Offsets.h"
#include "item/Record.h"
#include "macro/ShowTooltip.h"

#include <cstdint>

namespace Action::SlotItem {

namespace {

uint32_t ItemForSlot(int slot0) {
    if (slot0 < 0 || slot0 >= Offsets::ACTION_TABLE_MAX_SLOTS)
        return 0;
    const uint32_t entry = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ACTION_TABLE))[slot0];
    if ((entry & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_ITEM_BY_ID)
        return entry & Offsets::ACTION_PAYLOAD_MASK_ITEM_BY_ID;
    Macro::ShowTooltip::Info info;
    if (Macro::ShowTooltip::ForSlot(slot0, &info) &&
        info.target == Macro::ShowTooltip::Target::Item)
        return static_cast<uint32_t>(info.itemID);
    return 0;
}

// Reads the 1-based slot at stack[1]; false (after the usage error) when it
// is not a number.
bool SlotArg(void *L, const char *usage, int *slot0) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, usage);
        return false;
    }
    *slot0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    return true;
}

bool IsStackable(int slot0) {
    const uint8_t *record = ::Item::PeekRecord(ItemForSlot(slot0));
    return record != nullptr && Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_STACKABLE) > 1;
}

int __fastcall Script_IsStackableAction(void *L) {
    int slot0;
    if (!SlotArg(L, "Usage: IsStackableAction(slot)", &slot0))
        return 0;
    if (IsStackable(slot0))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_C_IsStackableAction(void *L) {
    int slot0;
    if (!SlotArg(L, "Usage: C_ActionBar.IsStackableAction(actionID)", &slot0))
        return 0;
    Game::Lua::PushBool(L, IsStackable(slot0));
    return 1;
}

int __fastcall Script_GetItemActionOnEquipSpellID(void *L) {
    int slot0;
    if (!SlotArg(L, "Usage: C_ActionBar.GetItemActionOnEquipSpellID(actionID)", &slot0))
        return 0;
    const uint8_t *record = ::Item::PeekRecord(ItemForSlot(slot0));
    if (record == nullptr)
        return 0;
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
        const uint32_t spellID = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4);
        const uint32_t trigger = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + i * 4);
        if (spellID != 0 && trigger == Offsets::ITEM_SPELLTRIGGER_ON_EQUIP) {
            Game::Lua::PushNumber(L, static_cast<double>(spellID));
            return 1;
        }
    }
    return 0;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSlotArgs[] = {
    Game::Doc::Req("slot", "number", "The action slot, 1 to 120."),
};
const Game::Doc::Field kIsStackableRets[] = {
    Game::Doc::Opt("isStackable", "number", nullptr,
                   "1 if the slot's item stacks, else nil."),
};
const Game::Doc::Function kIsStackableAction{
    "Returns 1 if an action slot holds an item that stacks.",
    kSlotArgs, kIsStackableRets, "ActionBar"};

const Game::Doc::Field kActionIDArgs[] = {
    Game::Doc::Req("actionID", "luaIndex"),
};
const Game::Doc::Field kCIsStackableRets[] = {
    Game::Doc::Req("isStackableAction", "bool"),
};
const Game::Doc::Function kCIsStackableAction{
    "Whether the action's item stacks.",
    kActionIDArgs, kCIsStackableRets};

const Game::Doc::Field kOnEquipRets[] = {
    Game::Doc::Opt("onEquipSpellID", "number", nullptr,
                   "Nil when the action has no item or its item has no on-equip spell."),
};
const Game::Doc::Function kGetItemActionOnEquipSpellID{
    "The spell the action's item grants while equipped.",
    kActionIDArgs, kOnEquipRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsStackableAction", &Script_IsStackableAction,
                                      &kIsStackableAction);
    Game::Lua::RegisterTableFunction("C_ActionBar", "IsStackableAction",
                                     &Script_C_IsStackableAction, &kCIsStackableAction);
    Game::Lua::RegisterTableFunction("C_ActionBar", "GetItemActionOnEquipSpellID",
                                     &Script_GetItemActionOnEquipSpellID,
                                     &kGetItemActionOnEquipSpellID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Action::SlotItem
