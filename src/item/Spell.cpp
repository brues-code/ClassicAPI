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

// `GetItemSpell(item)` — modern API returning `(spellName, spellID)`
// for items with an on-use spell effect (potions, trinkets, scrolls,
// hearthstone, food/drink with auras, etc.), or `nil` for items
// without one (gear with passive on-equip procs is excluded — those
// have trigger=1, not the on-use trigger=0 we look for).
//
// Vanilla 1.12 doesn't expose this; addons that needed it (e.g. to
// detect what spell a clicked trinket would fire, or to filter
// usable consumables) scraped the tooltip and parsed
// `ITEM_SPELL_TRIGGER_ONUSE` localized strings — locale-fragile and
// slow. This reads it directly off the cached `ItemStats_C` record.

#include "item/Spell.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Data.h"
#include "item/Location.h"
#include "item/Record.h"
#include "spell/Info.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Item::Spell {

namespace {

const char *SpellNameForID(int spellID) {
    const uint8_t *record = ::Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return nullptr;
    const int locale = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    auto *const *names = reinterpret_cast<const char *const *>(
        record + Offsets::OFF_SPELL_NAMES);
    const char *name = names[locale];
    return (name != nullptr && name[0] != '\0') ? name : nullptr;
}

// `GetItemSpell(itemID | "item:NNN" | itemLink)` — returns
// `(spellName, spellID)` for the on-use spell, or nothing (nil) for
// items without one. Auto-warms the cache on miss like
// `GetItemFamily` does — first call returns nil, subsequent calls
// after `GET_ITEM_INFO_RECEIVED` succeed.
int __fastcall Script_GetItemSpell(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }
    const int spellID = FindOnUseSpellIDInRecord(record);
    if (spellID <= 0)
        return 0;
    const char *name = SpellNameForID(spellID);
    if (name == nullptr)
        return 0;
    Game::Lua::PushString(L, name);
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 2;
}

// `IsHelpfulItem(item)` / `IsHarmfulItem(item)` — 1 when the item's on-use
// spell is helpful / harmful, else nil. Ports of 4.3.4's
// `Script_IsHelpfulItem` (0x008C8970) / `Script_IsHarmfulItem` (0x008C8A40),
// which classify `CGItem_C::GetUseSpell` with `SpellFaction`. 4.3.4 first
// tries a use-spell enchantment on a carried copy; 1.12 has no such
// enchantment type, so the record's on-use spell is the whole answer. The
// classifier is the one behind `IsHelpfulSpell` / `IsHarmfulSpell`, so an
// item and its spell always agree. Uncached item → nil, no load fired.
int OnUseSpellForArg(void *L) {
    const int itemID = Item::Location::IDForArg(L, 1, Offsets::INVMGR_SEARCH_BANK);
    return itemID > 0 ? OnUseSpellIDForItemID(static_cast<uint32_t>(itemID)) : 0;
}

int __fastcall Script_IsHelpfulItem(void *L) {
    if (::Spell::Info::IsHelpful(OnUseSpellForArg(L)))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_IsHarmfulItem(void *L) {
    if (::Spell::Info::IsHarmful(OnUseSpellForArg(L)))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kItemArgs[] = {
    Game::Doc::Req("item", "number", "An item ID, an item link, or the name of a carried item."),
};
const Game::Doc::Field kIsHelpfulRets[] = {
    Game::Doc::Opt("isHelpful", "number", nullptr,
                   "1 when the item's use effect aims at a friend, else nil."),
};
const Game::Doc::Function kIsHelpfulItem{
    "Whether the item's use effect aims at a friend.", kItemArgs, kIsHelpfulRets,
    "ItemGlobals"};

const Game::Doc::Field kIsHarmfulRets[] = {
    Game::Doc::Opt("isHarmful", "number", nullptr,
                   "1 when the item's use effect aims at an enemy, else nil."),
};
const Game::Doc::Function kIsHarmfulItem{
    "Whether the item's use effect aims at an enemy.", kItemArgs, kIsHarmfulRets,
    "ItemGlobals"};

} // namespace

// Walks the item's 5 spell slots looking for the first ON_USE
// (trigger=0) entry. Modern `GetItemSpell` returns only this kind;
// passive-on-equip procs (trigger=1), weapon procs (trigger=2),
// soulstones (trigger=4), and recipe-learn entries (trigger=6) all
// stay invisible to the API.
//
// Vanilla server data on Turtle WoW has been observed to put the
// ON_USE entry at slot index 0 for most items, but a few customized
// items use later slots, so we scan all 5.
int FindOnUseSpellIDInRecord(const uint8_t *record) {
    auto *spellIDs = reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_SPELL_ID);
    auto *triggers = reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_SPELL_TRIGGER);
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; i++) {
        if (spellIDs[i] != 0 &&
            triggers[i] == Offsets::ITEM_SPELLTRIGGER_ON_USE) {
            return static_cast<int>(spellIDs[i]);
        }
    }
    return 0;
}

int OnUseSpellIDForItemID(uint32_t itemID) {
    if (itemID == 0)
        return 0;
    const uint8_t *record = Item::PeekRecord(itemID);
    if (record == nullptr)
        return 0;
    return FindOnUseSpellIDInRecord(record);
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSpell", &Script_GetItemSpell);
    Game::Lua::RegisterGlobalFunction("IsHelpfulItem", &Script_IsHelpfulItem, &kIsHelpfulItem);
    Game::Lua::RegisterGlobalFunction("IsHarmfulItem", &Script_IsHarmfulItem, &kIsHarmfulItem);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Spell
