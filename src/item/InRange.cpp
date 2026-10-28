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

// `C_Item.IsItemInRange(item, targetUnit)` — true if the item is in
// range of the unit, false if out of range, nil when the range check
// doesn't apply. `item` is an itemID, `"item:NNN"` string, or item link
// (names aren't resolvable — vanilla has no name→ID map — and yield nil);
// `targetUnit` is a unit token ("target", "mouseover", "party1", …).
//
// `C_Item.ItemHasRange(item)` / `ItemHasRange(item)` — whether the item
// has a range restriction at all (same `item` shapes). The global keeps
// 3.3.5's 1/nil; the namespaced form returns a bool.
//
// An item's range comes from the spell it fires on use: we resolve the
// item to its on-use spell (`Item::Spell::OnUseSpellIDForItemID`) and run
// the same check as `C_Spell.IsSpellInRange`, via the shared
// `Spell::Range::CastVsUnit` core (4.3.4's Spell_C_RangeCheckSelected). So items with a targeted on-use
// effect that has a range (nets, bombs, thrown/targeted quest items, …)
// get a true/false answer; items with no on-use spell, or whose on-use
// spell is rangeless (potions, food, self-buff trinkets, hearthstone),
// return nil — matching retail, which also reports nil for items without
// a range restriction.
//
// Like `IsSpellInRange`: nil also for a unit the on-use spell cannot be cast
// at; line of sight is not checked. Passive reader — an item not yet in the client item cache
// returns nil (no background fetch); items you'd range-check are normally
// in bags / on the action bar and already cached.

#include "Game.h"
#include "item/Arg.h"
#include "item/Spell.h"
#include "spell/Range.h"

#include <cstdint>

namespace Item::InRange {

namespace {

int __fastcall Script_C_Item_IsItemInRange(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    const int spellID =
        itemID > 0 ? Item::Spell::OnUseSpellIDForItemID(
                         static_cast<uint32_t>(itemID))
                   : 0;
    const char *unit =
        Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;

    // `::Spell` — unqualified `Spell` would bind to `Item::Spell` here.
    const int result = ::Spell::Range::CastVsUnit(spellID, unit, false);
    if (result < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushBool(L, result == 1);
    return 1;
}

// Shared core: true iff the item's on-use spell reports a non-near-zero
// min or max range via the engine's own resolver. A missing record, an
// item with no on-use spell, and a rangeless spell all read as false —
// passive, no cache warm, matching 3.3.5's `Script_ItemHasRange`.
bool ItemHasRange(int itemID) {
    const int spellID =
        itemID > 0 ? Item::Spell::OnUseSpellIDForItemID(
                         static_cast<uint32_t>(itemID))
                   : 0;
    // `::Spell` — unqualified `Spell` would bind to `Item::Spell` here.
    return ::Spell::Range::SpellIDHasRange(spellID);
}

int __fastcall Script_ItemHasRange(void *L) {
    if (ItemHasRange(Item::Arg::ResolveItemID(L, 1)))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_C_Item_ItemHasRange(void *L) {
    Game::Lua::PushBool(L, ItemHasRange(Item::Arg::ResolveItemID(L, 1)));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "IsItemInRange",
                                     &Script_C_Item_IsItemInRange);
    Game::Lua::RegisterGlobalFunction("ItemHasRange", &Script_ItemHasRange);
    Game::Lua::RegisterTableFunction("C_Item", "ItemHasRange",
                                     &Script_C_Item_ItemHasRange);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::InRange
