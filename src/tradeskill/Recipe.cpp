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
//
// Mirrors 3.3.5's Script_GetTradeSkillRecipeLink (0x005DB920):
//   - the index is the row's 1-based position, truncated and compared
//     unsigned against the row count, as every trade-skill row getter does
//     (1.12's GetTradeSkillItemLink at 0x004FF410: __ftol, dec, `jnc`);
//   - the row's spell must have a Spell.dbc record with SPELL_ATTR_TRADESPELL,
//     so a header row (spell ID -1) returns nothing;
//   - the link is `|cffffd000|Henchant:<id>|h[<skill line>: <name>]|h|r`,
//     or `[<name>]` when no skill line is found. The skill line comes from
//     the SkillLineAbility row for the player's race and class, through the
//     engine's own lookup (FUN_SKILL_LINE_ABILITY_LOOKUP), which 1.12's
//     trade-skill rebuild calls with the same arguments. Names are pushed
//     as the record holds them, empty or not, as 3.3.5 does.
// The link format is 3.3.5's literal; 1.12's own `enchant:` format
// (0x0084D410, white, bare name) is GetCraftItemLink's and is not this one.
// Tooltip::Hyperlink shows these links, which 1.12's SetHyperlink rejects.
//
// `IsTradeSkillLinked()` says whether the window shows a profession opened
// from another player's link, and whose. On this client it never does. A
// received `trade:` link opens ClassicAPI's own viewer
// (Util/TradeSkillLink.lua), which the trade-skill functions never describe,
// and the window shows the player's own professions only. So it returns
// (nil, nil), 3.3.5's answer for the player's own window. Addons check it to
// keep a linked profession out of the player's own records; Zygor skips its
// recipe scan on it.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>

namespace TradeSkill::Recipe {

namespace {

using AbilityLookup_t = const uint8_t *(__fastcall *)(char race, char classId,
                                                      uint32_t spellID);

// A 9-locale string field, in the client's locale; "" for an empty slot.
const char *LocalizedName(const uint8_t *record, int offset) {
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name = Game::Read<const char *>(record, offset + locale * 4);
    return name != nullptr ? name : "";
}

// The SkillLine.dbc record `spellID` belongs to for the player's race and
// class, or nullptr. Reads race and class from the player's descriptor, as
// the trade-skill rebuild FUN_004fca20 does before the same lookup.
const uint8_t *PlayerSkillLineFor(int spellID) {
    auto *player = static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_PLAYER, Unit::Identity::PlayerGuid()));
    if (player == nullptr)
        return nullptr;
    auto *desc = Game::Read<const uint8_t *>(player, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return nullptr;

    const uint8_t *ability = reinterpret_cast<AbilityLookup_t>(
        Offsets::FUN_SKILL_LINE_ABILITY_LOOKUP)(
        static_cast<char>(desc[Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE]),
        static_cast<char>(desc[Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE]),
        static_cast<uint32_t>(spellID));
    if (ability == nullptr)
        return nullptr;
    return DBC::Record(Offsets::VAR_SKILL_LINE_RECORDS, Offsets::VAR_SKILL_LINE_COUNT,
                       Game::Read<uint32_t>(ability, Offsets::OFF_SLA_SKILL_ID));
}

int __fastcall Script_GetTradeSkillRecipeLink(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTradeSkillRecipeLink(index)");
        return 0;
    }
    // The engine truncates, subtracts 1 and compares unsigned against the
    // row count, so only an index that truncates into [1, count] passes.
    // Compared as a double first: casting NaN or an out-of-range value to
    // an integer is undefined in C++.
    const double n = Game::Lua::ToNumber(L, 1);
    const uint32_t count = Game::Read<uint32_t>(Offsets::VAR_TRADESKILL_COUNT);
    if (!(n >= 1.0 && n < static_cast<double>(count) + 1.0))
        return 0;

    const int spellID = Spell::Lookup::RecipeSlotSpellID(
        Offsets::VAR_TRADESKILL_ENTRIES, Offsets::VAR_TRADESKILL_COUNT,
        static_cast<int>(n) - 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr ||
        (Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
         Offsets::SPELL_ATTR_TRADESPELL) == 0)
        return 0;

    const char *name = LocalizedName(record, Offsets::OFF_SPELL_NAMES);
    char link[1024];
    if (const uint8_t *skillLine = PlayerSkillLineFor(spellID)) {
        std::snprintf(link, sizeof link, "|cffffd000|Henchant:%d|h[%s: %s]|h|r", spellID,
                      LocalizedName(skillLine, Offsets::OFF_SKILL_LINE_NAME), name);
    } else {
        std::snprintf(link, sizeof link, "|cffffd000|Henchant:%d|h[%s]|h|r", spellID, name);
    }

    Game::Lua::PushString(L, link);
    return 1;
}

int __fastcall Script_IsTradeSkillLinked(void *L) {
    Game::Lua::PushNil(L);
    Game::Lua::PushNil(L);
    return 2;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kRecipeLinkArgs[] = {
    Game::Doc::Req("index", "luaIndex", "A row of the trade-skill window."),
};
const Game::Doc::Field kRecipeLinkRets[] = {
    Game::Doc::Opt("link", "string", nullptr,
                   "An enchant: link to the row's recipe spell; nil for a header row."),
};
const Game::Doc::Function kGetTradeSkillRecipeLink{
    "Returns a link to the recipe spell in a row of the trade-skill window.",
    kRecipeLinkArgs, kRecipeLinkRets, "TradeSkillGlobals"};

const Game::Doc::Field kLinkedRets[] = {
    Game::Doc::Opt("isLinked", "bool", nullptr,
                   "True when the window shows another player's linked profession."),
    Game::Doc::Opt("linkedPlayerName", "string", nullptr,
                   "The name of the player whose profession is shown."),
};
const Game::Doc::Function kIsTradeSkillLinked{
    "Whether the trade-skill window shows a profession opened from another player's link.",
    {}, kLinkedRets, "TradeSkillGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTradeSkillRecipeLink", &Script_GetTradeSkillRecipeLink,
                                      &kGetTradeSkillRecipeLink);
    Game::Lua::RegisterGlobalFunction("IsTradeSkillLinked", &Script_IsTradeSkillLinked,
                                      &kIsTradeSkillLinked);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace TradeSkill::Recipe
