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

// `C_Item.UseAtUnit(item, unit [, placeGroundSpell])` — the item analog of
// `C_Spell.CastAtUnit`, and the same flow:
//
//   - A ground-target item (Iron Grenade, demolition charges, etc.) is
//     placed at the unit's feet, no reticle click — or, with
//     `placeGroundSpell` false, its reticle is left for the player.
//   - A unit-target item (a bandage, a scroll) fires straight at the unit,
//     whatever is selected.
//
// `item` is an item location (`{bagID=B, slotIndex=S}`,
// `{equipmentSlotIndex=N}`, an item GUID) or an item reference (itemID,
// `item:N`, a link, the name of an item you carry) — see
// `Item::Location::ResolveItemArgOrLocation`. A location names one exact
// item, which is what a caller holding two stacks of the same thing needs.
//
// The token resolves once to the unit object, read for both its position and
// its GUID; the GUID is the implicit target of the use. After a use the
// engine reports as begun, `Spell::Placement::FinishAtUnit` handles the
// placement branch exactly as it does for a spell.
//
// Returns true when the item went out at the unit (fired, placed, or its
// reticle left up on request); false for unparseable input, an item the
// player doesn't have, a use the engine refuses, an absent unit, or a unit
// the item's spell won't accept.

#include "Game.h"
#include "item/Location.h"
#include "item/Use.h"
#include "spell/Placement.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cstdint>

namespace Item::UseAtUnit {

namespace {

int __fastcall Script_C_Item_UseAtUnit(void *L) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    // Read every argument before the item resolve, which stomps the Lua
    // stack. Omitted or nil `placeGroundSpell` means place it; only an
    // explicit false holds the reticle (`lua_type` reports NONE, not nil,
    // past the top of the stack, so test the top first).
    const bool placeGround = Game::Lua::GetTop(L) < 3 ||
                             Game::Lua::Type(L, 3) == Game::Lua::TYPE_NIL ||
                             Game::Lua::ToBoolean(L, 3) != 0;

    // Resolve the unit first — fail fast before firing the item if the unit
    // is absent. A string that is no unit token raises "Unknown unit", as
    // `C_Spell.CastAtUnit` does.
    void *unitObj = Unit::Position::ResolveToken(Game::Lua::ToString(L, 2));
    float pos[3];
    if (unitObj == nullptr || !Unit::Position::Read(unitObj, pos)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uint64_t targetGuid = Unit::Identity::GuidForObject(unitObj);

    const uint8_t *item = Item::Location::ResolveItemArgOrLocation(L, 1);
    if (item == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    // Only a placing call clears an older reticle first — see
    // `C_Spell.CastAtUnit`.
    if (placeGround)
        Spell::Placement::Stop();
    const bool ok = Item::Use::UseItem(item, targetGuid) &&
                    Spell::Placement::FinishAtUnit(pos, placeGround);
    Game::Lua::PushBool(L, ok);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kUseAtUnitArgs[] = {
    Game::Doc::Req("item", "ItemInfo",
                   "An item ID, an item link, the name of a carried item, or an item location."),
    Game::Doc::Req("unit", "UnitToken", "The unit to use the item at."),
    Game::Doc::Opt("placeGroundSpell", "bool", "true",
                   "False leaves a ground-target item's reticle for you to click."),
};
const Game::Doc::Field kUseAtUnitRets[] = {
    Game::Doc::Req("used", "bool",
                   "True when the item went out at the unit; false when you do not carry "
                   "it, it cannot be used now, or the unit is not a target it accepts."),
};
const Game::Doc::Function kUseAtUnit{
    "Uses an item at a unit, dropping a ground-target item at that unit's feet.",
    kUseAtUnitArgs, kUseAtUnitRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "UseAtUnit",
                                     &Script_C_Item_UseAtUnit, &kUseAtUnit);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::UseAtUnit
