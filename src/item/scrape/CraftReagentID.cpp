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

// Vanilla splits the "craft" UI surface (enchanting, beast training)
// from the "tradeskill" UI (smithing, alchemy, etc.) into independent
// frame storage with independent globals — see `VAR_CRAFT_*` vs
// `VAR_TRADESKILL_*` in `Offsets.h`. The recipe spell records both UIs
// reference still live in the same `Spell.dbc`.

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

namespace Item::CraftReagentID {

namespace {

// `GetCraftReagentItemID(craftIndex, reagentIndex)` — itemID
// companion to `GetCraftReagentItemLink`. nil for OOR indices or
// when there's no Nth reagent.
int __fastcall Script_GetCraftReagentItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GetCraftReagentItemID(craftIndex, reagentIndex)");
        return 0;
    }
    const int craftIndex = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const int reagentIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));

    const int spellID = Spell::Lookup::RecipeSlotSpellID(
        Offsets::VAR_CRAFT_ENTRIES, Offsets::VAR_CRAFT_COUNT, craftIndex);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    const int itemID = Spell::Lookup::NthRecipeReagentItemID(record, reagentIndex);
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("craftIndex", "luaIndex"),
    Game::Doc::Req("reagentIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when the craft or the reagent does not exist."),
};
const Game::Doc::Function kGetCraftReagentItemID{
    "Returns the item ID of a reagent of a craft window recipe, the ID that "
    "GetCraftReagentItemLink shows.",
    kArgs, kRets, "ItemGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCraftReagentItemID",
                                      &Script_GetCraftReagentItemID, &kGetCraftReagentItemID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::CraftReagentID
