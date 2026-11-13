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

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

namespace Item::TradeSkillReagentID {

namespace {

// `GetTradeSkillReagentItemID(index, reagentIndex)` — itemID
// companion to `GetTradeSkillReagentItemLink`. nil for OOR indices
// or when there's no Nth reagent.
int __fastcall Script_GetTradeSkillReagentItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GetTradeSkillReagentItemID(index, reagentIndex)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    const int reagentIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));

    const int spellID = Spell::Lookup::RecipeSlotSpellID(
        Offsets::VAR_TRADESKILL_ENTRIES, Offsets::VAR_TRADESKILL_COUNT, index);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    const int itemID = Spell::Lookup::NthRecipeReagentItemID(record, reagentIndex);
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("index", "luaIndex", "A recipe of the open trade skill window."),
    Game::Doc::Req("reagentIndex", "luaIndex"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when the recipe has no reagent at that index."),
};
const Game::Doc::Function kGetTradeSkillReagentItemID{
    "Returns a reagent item of a trade skill recipe.", kArgs, kRets,
    "ItemGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTradeSkillReagentItemID",
                                      &Script_GetTradeSkillReagentItemID,
                                      &kGetTradeSkillReagentItemID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::TradeSkillReagentID
