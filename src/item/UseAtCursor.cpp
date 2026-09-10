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

// `C_Item.UseAtCursor(item)` — use the given item at the player's
// current cursor world position. For items whose on-use spell is
// ground-target (Iron Grenade, demolition charges, etc.), bypasses
// the manual click on the AoE reticle that the engine would
// otherwise require.
//
// `item` is an item location (`{bagID=B, slotIndex=S}`,
// `{equipmentSlotIndex=N}`, an item GUID) or an item reference (itemID,
// `item:N`, a link, the name of an item you carry) — see
// `Item::Location::ResolveItemArgOrLocation`. A location names one exact
// item, which is what a caller holding two stacks of the same thing needs.
//
// Mirrors `C_Item.UseItemByName`'s resolve + `FUN_ITEM_USE` dispatch
// — the engine's item-use path eventually hands the on-use spell to
// the same cast dispatcher that ground-target spells go through, so
// placement-mode activation works identically. After firing the
// item, `Spell::AtCursor::Resolve()` commits the placement at cursor.
//
// Returns `true` when the cursor-placement leg landed; `false` for
// items that aren't ground-target (the item still fires, just with
// no implicit target), unparseable input, an item the player doesn't
// have, etc.

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"
#include "spell/AtCursor.h"

#include <cstdint>

namespace Item::UseAtCursor {

namespace {

using UseItem_t = unsigned(__thiscall *)(const void *item, const uint64_t *targetGuid,
                                          int flag);

int __fastcall Script_C_Item_UseAtCursor(void *L) {
    const uint8_t *item = Item::Location::ResolveItemArgOrLocation(L, 1);
    if (item == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    const uint64_t zeroTarget = 0;
    auto useItem = reinterpret_cast<UseItem_t>(Offsets::FUN_ITEM_USE);
    useItem(item, &zeroTarget, 0);

    const bool placed = Spell::AtCursor::Resolve();
    Game::Lua::PushBool(L, placed);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "UseAtCursor",
                                     &Script_C_Item_UseAtCursor);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::UseAtCursor
