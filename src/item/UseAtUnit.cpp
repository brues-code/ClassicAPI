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

// `C_Item.UseAtUnit(item, unit)` — use the given item at the given unit's
// feet. For items whose on-use spell is ground-target (Iron Grenade,
// demolition charges, etc.), bypasses the manual click on the AoE reticle.
// `UseAtUnit(item, "target")` drops the reticle at the target's position.
//
// `item` is an item location (`{bagID=B, slotIndex=S}`,
// `{equipmentSlotIndex=N}`, an item GUID) or an item reference (itemID,
// `item:N`, a link, the name of an item you carry) — see
// `Item::Location::ResolveItemArgOrLocation`. A location names one exact
// item, which is what a caller holding two stacks of the same thing needs.
//
// Unit-position analog of `C_Item.UseAtCursor` — same resolve +
// `FUN_ITEM_USE` dispatch, but the placement is committed at the
// unit's world position (via `Spell::AtCursor::CommitAtCoords`)
// instead of the cursor raycast.
//
// Returns `true` when the placement landed at the unit; `false` for
// non-ground-target items (which still fire, at the unit), unparseable
// input, an item the player doesn't have, or an unresolvable unit.

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"
#include "spell/AtCursor.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cstdint>

namespace Item::UseAtUnit {

namespace {

using UseItem_t = unsigned(__thiscall *)(const void *item, const uint64_t *targetGuid,
                                          int flag);

int __fastcall Script_C_Item_UseAtUnit(void *L) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const char *token = Game::Lua::ToString(L, 2);

    // Resolve the unit position first — fail fast before firing the
    // item if the unit is absent. (The item resolve below stomps the
    // Lua stack, so read the token argument before that runs.)
    float pos[3];
    if (!Unit::Position::ReadToken(token, pos)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    // Dispatch with the unit's GUID as the implicit target, exactly as
    // `C_Spell.CastAtUnit` does. A ground-target item ignores it and enters
    // placement, committed below; a unit-target one (a bandage, a scroll)
    // fires straight at the unit instead of at whatever is selected. Read
    // before the item resolve, which stomps the Lua stack.
    const uint64_t targetGuid = Unit::Identity::GuidForToken(token);

    const uint8_t *item = Item::Location::ResolveItemArgOrLocation(L, 1);
    if (item == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    auto useItem = reinterpret_cast<UseItem_t>(Offsets::FUN_ITEM_USE);
    useItem(item, &targetGuid, 0);

    const bool placed = Spell::AtCursor::CommitAtCoords(pos);
    Game::Lua::PushBool(L, placed);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "UseAtUnit",
                                     &Script_C_Item_UseAtUnit);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::UseAtUnit
