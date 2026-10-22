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

// `IsConsumableItem(item)` / `C_Item.IsConsumableItem(item)` — whether using
// the item uses it up. A port of 3.3.5's `Script_IsConsumableItem`
// (0x005173E0); 4.3.4's (0x008DA710) is the same minus ammo, which Cata
// removed. An item is consumable when it is ammo or thrown (inventory type 24
// or 25), or when one of its on-use spells (trigger 0) either has negative
// charges, so the item goes when they run out, or uses reagents. Item class
// plays no part: Rough Dynamite (Trade Goods) is consumable and an on-use
// trinket is not.
//
// This binary has the same test without the reagent half, in
// `ActionSlotIsConsumable` (0x004E5250) behind `IsConsumableAction`.
//
// The retail client is no oracle for this one: its IsConsumableItem is
// currently broken and returns false even for potions.
//
// The argument is resolved as 3.3.5's `GetItemIDByName` does: a number, an
// `item:` string or link, or the name of a carried item (`IDForName` stands
// in for 3.3.5's `s_itemByName`). An uncached item is not consumable and
// fires no load — call `C_Item.RequestLoadItemDataByID` first if needed.
//
// The global keeps 3.3.5's 1/nil; `C_Item.IsConsumableItem` returns a bool.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Location.h"
#include "item/Record.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Item::Consumable {

namespace {

bool UsesReagents(const uint8_t *spell) {
    for (int i = 0; i < Offsets::SPELL_MAX_REAGENTS; ++i) {
        if (Game::Read<int>(spell, Offsets::OFF_SPELL_REAGENT_ID + i * 4) != 0 &&
            Game::Read<int>(spell, Offsets::OFF_SPELL_REAGENT_COUNT + i * 4) > 0)
            return true;
    }
    return false;
}

bool IsConsumable(void *L) {
    const Item::Arg::Resolved arg = Item::Arg::Resolve(L, 1);
    int itemID = arg.itemID;
    if (itemID <= 0)
        itemID = Item::Location::IDForName(arg.name, Offsets::INVMGR_SEARCH_BANK);
    const uint8_t *record =
        itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
    if (record == nullptr)
        return false;

    const uint32_t invType = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    if (invType == Offsets::INVTYPE_AMMO || invType == Offsets::INVTYPE_THROWN)
        return true;

    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
        const int spellID = Game::Read<int>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4);
        if (spellID == 0 ||
            Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + i * 4) !=
                Offsets::ITEM_SPELLTRIGGER_ON_USE)
            continue;
        const uint8_t *spell = Spell::Lookup::RecordForID(spellID);
        if (spell == nullptr)
            continue;
        if (Game::Read<int>(record, Offsets::OFF_ITEMSTATS_SPELL_CHARGES + i * 4) < 0 ||
            UsesReagents(spell))
            return true;
    }
    return false;
}

int __fastcall Script_IsConsumableItem(void *L) {
    if (IsConsumable(L))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_C_Item_IsConsumableItem(void *L) {
    Game::Lua::PushBool(L, IsConsumable(L));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kLegacyArgs[] = {
    Game::Doc::Req("item", "number", "An item ID, an item link, or the name of a carried item."),
};
const Game::Doc::Field kLegacyRets[] = {
    Game::Doc::Opt("isConsumable", "number", nullptr, "1 when using the item uses it up, else nil."),
};
const Game::Doc::Function kIsConsumableItem{
    "Whether using the item uses it up: ammo, thrown weapons, and items whose use has charges "
    "that run out or costs reagents.",
    kLegacyArgs, kLegacyRets, "ItemGlobals"};

const Game::Doc::Field kModernArgs[] = {
    Game::Doc::Req("item", "ItemInfo", "An item ID, an item link, or the name of a carried item."),
};
const Game::Doc::Field kModernRets[] = {
    Game::Doc::Req("isConsumable", "bool", "True when using the item uses it up."),
};
const Game::Doc::Function kCIsConsumableItem{
    "Whether using the item uses it up: ammo, thrown weapons, and items whose use has charges "
    "that run out or costs reagents.",
    kModernArgs, kModernRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsConsumableItem", &Script_IsConsumableItem,
                                      &kIsConsumableItem);
    Game::Lua::RegisterTableFunction("C_Item", "IsConsumableItem",
                                     &Script_C_Item_IsConsumableItem, &kCIsConsumableItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::Consumable
