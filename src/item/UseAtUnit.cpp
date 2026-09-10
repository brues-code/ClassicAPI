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

// `C_Item.UseAtUnit(itemInfo, unit)` — use the named item at the given
// unit's feet. For items whose on-use spell is ground-target (Iron
// Grenade, demolition charges, etc.), bypasses the manual click on the
// AoE reticle. `UseAtUnit(itemInfo, "target")` drops the reticle at
// the target's position.
//
// Unit-position analog of `C_Item.UseAtCursor` — same bag-walk +
// `FUN_ITEM_USE` dispatch, but the placement is committed at the
// unit's world position (via `Spell::AtCursor::CommitAtCoords`)
// instead of the cursor raycast.
//
// Returns `true` when the placement landed at the unit; `false` for
// non-ground-target items (which still fire, at the unit), unparseable
// input, item-not-in-bags, or an unresolvable unit.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
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
    const auto arg = Item::Arg::Resolve(L, 1);
    if (arg.itemID <= 0 && arg.name == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const char *token = Game::Lua::ToString(L, 2);

    // Resolve the unit position first — fail fast before firing the
    // item if the unit is absent. (FindByArgInBags below stomps the
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
    // before `FindByArgInBags`, which stomps the Lua stack.
    const uint64_t targetGuid = Unit::Identity::GuidForToken(token);

    Item::Location::ByGUIDResult found;
    if (!Item::Location::FindByArgInBags(L, arg, &found)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    auto useItem = reinterpret_cast<UseItem_t>(Offsets::FUN_ITEM_USE);
    useItem(found.item, &targetGuid, 0);

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
