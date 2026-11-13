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

namespace Item::TradeSkillID {

namespace {

// `GetTradeSkillItemID(index)` — itemID companion to
// `GetTradeSkillItemLink`. Returns the recipe's produced-item itemID
// (the `CreatedItem` field on the spell record). nil when the
// tradeskill UI is closed, the index is OOR, or the recipe doesn't
// produce a finished item (e.g. enchanting scrolls have a 0 here in
// vanilla — their effect targets equipment directly).
int __fastcall Script_GetTradeSkillItemID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTradeSkillItemID(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;

    const int spellID = Spell::Lookup::RecipeSlotSpellID(
        Offsets::VAR_TRADESKILL_ENTRIES, Offsets::VAR_TRADESKILL_COUNT, index);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;

    const int itemID = static_cast<int>(*reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_SPELL_RECORD_CREATED_ITEM));
    if (itemID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("index", "luaIndex", "A recipe of the open trade skill window."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("itemID", "number", nullptr,
                   "Nil when the window is closed, the index is out of range, or the recipe "
                   "makes no item."),
};
const Game::Doc::Function kGetTradeSkillItemID{
    "Returns the item that a trade skill recipe makes.", kArgs, kRets,
    "ItemGlobals", true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTradeSkillItemID", &Script_GetTradeSkillItemID,
                                      &kGetTradeSkillItemID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::TradeSkillID
