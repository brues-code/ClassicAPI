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
// Same chain as `C_Spell.CastAtCursor`: cancel any placement already up,
// use the item (`Item::Use::UseItem`, whose on-use spell goes through the
// same cast attempt a ground spell does), then commit the placement at the
// cursor. The commit runs only when the engine reports the item's spell
// began, so a refused use never lands anything.
//
// Returns whether the item's spell went out at the cursor; `false` for
// items that aren't ground-target (the item still fires, just with
// no implicit target), unparseable input, an item the player doesn't
// have, etc.

#include "Game.h"
#include "item/Location.h"
#include "item/Use.h"
#include "spell/Placement.h"

#include <cstdint>

namespace Item::UseAtCursor {

namespace {

int __fastcall Script_C_Item_UseAtCursor(void *L) {
    const uint8_t *item = Item::Location::ResolveItemArgOrLocation(L, 1);
    if (item == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    Spell::Placement::Stop();
    const bool used = Item::Use::UseItem(item, 0);
    Game::Lua::PushBool(L, used && Spell::Placement::CommitAtCursor());
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kUseAtCursorArgs[] = {
    Game::Doc::Req("item", "ItemInfo",
                   "An item ID, an item link, the name of a carried item, or an item location."),
};
const Game::Doc::Field kUseAtCursorRets[] = {
    Game::Doc::Req("placed", "bool",
                   "True when the item's effect landed at the cursor; false when the cursor "
                   "was not on the ground, or the item needs no ground target."),
};
const Game::Doc::Function kUseAtCursor{
    "Uses a ground-target item at the cursor's world position, with no reticle click.",
    kUseAtCursorArgs, kUseAtCursorRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "UseAtCursor",
                                     &Script_C_Item_UseAtCursor, &kUseAtCursor);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::UseAtCursor
