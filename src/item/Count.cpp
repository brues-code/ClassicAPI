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

// `C_Item.GetItemCount(itemInfo [, includeBank [, includeUses]])` — a
// straight port of 3.3.5's `Script_GetItemCount` (0x0051C2E0; 4.3.4's is
// identical) onto 1.12's own inventory searches, which are the ancestors of
// 3.3.5's `CGBag_C::FindItem` family with the same flag bits:
//
//   total = GetItemTypeCount(itemID, includeBank ? BANK : 0)
//         → FUN_INVMGR_COUNT_STACKS_BY_ID
//   includeUses, for a non-stackable item whose base charges are negative:
//   total = GetItemTypeCharges(itemID, 0)   → FUN_INVMGR_COUNT_CHARGES_BY_ID
//   and if the use spell's reagents allow N casts, total = N.
//
// So the count is the engine's: equipment, the equipped bags themselves,
// backpack, keyring and bag contents (plus the bank, read without a banker),
// broken items skipped. 1.12's walk has no 3.3.5 IGNORE_ITEM_STATE (0x80)
// bit, so includeUses can't count broken items the way 3.3.5 does. 3.3.5's
// use-enchant branch (`FindUseEnchantItemPtr`) is not ported; base charges
// and the base use spell stand for it.

#include "item/Count.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Location.h"
#include "item/Record.h"
#include "spell/CastCount.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Item::Count {

namespace {

using CountByID_t = int(__thiscall *)(const void *invMgr, int itemID, uint32_t flags);
using LooseBool_t = int(__fastcall *)(void *L, int idx, int defaultValue);

int CountByID(uintptr_t fn, int itemID, uint32_t flags) {
    const uint8_t *invMgr = Unit::Identity::PlayerInventoryManager();
    if (invMgr == nullptr)
        return 0;
    return reinterpret_cast<CountByID_t>(fn)(invMgr, itemID, flags);
}

// 3.3.5's `CGItem_C::NumBaseCharges` / `GetUseSpell`: the first on-use
// spell slot's charges and spell.
struct UseSpell {
    int spellID;
    int charges;
};

UseSpell FirstUseSpell(const uint8_t *record) {
    for (int i = 0; i < Offsets::ITEMSTATS_SPELL_SLOT_COUNT; ++i) {
        const int spellID = Game::Read<int>(record, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4);
        const uint32_t trigger =
            Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + i * 4);
        if (spellID != 0 && trigger == Offsets::ITEM_SPELLTRIGGER_ON_USE)
            return {spellID, Game::Read<int>(record, Offsets::OFF_ITEMSTATS_SPELL_CHARGES + i * 4)};
    }
    return {0, 0};
}

int CountUses(int itemID, int total) {
    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr || Game::Read<int>(record, Offsets::OFF_ITEMSTATS_STACKABLE) > 1)
        return total;
    const UseSpell use = FirstUseSpell(record);
    if (use.charges < 0)
        total = CountByID(Offsets::FUN_INVMGR_COUNT_CHARGES_BY_ID, itemID, 0);
    if (const int casts = ::Spell::CastCount::ForSpell(use.spellID))
        total = casts;
    return total;
}

int __fastcall Script_C_Item_GetItemCount(void *L) {
    const auto looseBool = reinterpret_cast<LooseBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE);
    const bool includeBank = looseBool(L, 2, 0) != 0;
    const bool includeUses = looseBool(L, 3, 0) != 0;
    const uint32_t flags = includeBank ? Offsets::INVMGR_SEARCH_BANK : 0;

    const Item::Arg::Resolved arg = Item::Arg::Resolve(L, 1);
    int itemID = arg.itemID;
    if (itemID <= 0)
        itemID = Item::Location::IDForName(arg.name, flags);

    int total = itemID > 0 ? CountByID(Offsets::FUN_INVMGR_COUNT_STACKS_BY_ID, itemID, flags) : 0;
    if (total != 0 && includeUses)
        total = CountUses(itemID, total);

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(total));
    return 1;
}

} // namespace

int InInventory(int itemID) {
    if (itemID <= 0)
        return 0;
    return CountByID(Offsets::FUN_INVMGR_COUNT_STACKS_BY_ID, itemID, 0);
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemCount",
                                     &Script_C_Item_GetItemCount);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Item::Count
