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

// The trade-skill window's recipe functions from 3.x:
// `GetTradeSkillRecipeLink(index)` and `IsTradeSkillLinked()`.
//
// `GetTradeSkillRecipeLink(index)` links the recipe in row `index`: the
// recipe spell, as an `enchant:` link. Vanilla's window links only the item a
// recipe makes (`GetTradeSkillItemLink`), which doesn't say which recipe it
// is. Addons that record known recipes by spell ID read the ID from this
// link: Zygor Guides Viewer 2.0 scans every row with it on TRADE_SKILL_SHOW.
// The link has the form the client's own `GetCraftItemLink` (0x004F72A0)
// gives an enchanting formula, `|cffffffff|Henchant:<spellID>|h[<name>]|h|r`
// (format at 0x0084D410) with the spell's name in the client's locale, so
// this client's chat and ItemRefTooltip show it. The row is read the way the
// engine's row functions read it: a header row (spell ID -1) or an index
// outside the list returns nothing.
//
// `IsTradeSkillLinked()` says whether the window shows a profession opened
// from another player's link. On this client it never does. A received
// `trade:` link opens ClassicAPI's own viewer (Util/TradeSkillLink.lua),
// which the trade-skill functions never describe, and the window shows the
// player's own professions only. So it returns nil, the answer for the
// player's own window. Addons check it to keep a linked profession out of
// the player's own records; Zygor skips its recipe scan on it.

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

#include <cstdint>
#include <cstdio>

namespace TradeSkill::Recipe {

namespace {

int __fastcall Script_GetTradeSkillRecipeLink(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTradeSkillRecipeLink(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;

    const int spellID = Spell::Lookup::RecipeSlotSpellID(
        Offsets::VAR_TRADESKILL_ENTRIES, Offsets::VAR_TRADESKILL_COUNT, index);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;

    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name = Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);
    if (name == nullptr || *name == '\0')
        return 0;

    char link[512];
    const int n = std::snprintf(link, sizeof link, "|cffffffff|Henchant:%d|h[%s]|h|r", spellID, name);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof link)
        return 0;

    Game::Lua::PushString(L, link);
    return 1;
}

int __fastcall Script_IsTradeSkillLinked(void *L) {
    Game::Lua::PushNil(L);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTradeSkillRecipeLink", &Script_GetTradeSkillRecipeLink);
    Game::Lua::RegisterGlobalFunction("IsTradeSkillLinked", &Script_IsTradeSkillLinked);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace TradeSkill::Recipe
