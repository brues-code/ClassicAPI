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

// `C_SpellBook.GetNumSpellBookSkillLines` / `GetSpellBookSkillLineInfo` /
// `GetSpellBookItemSkillLineIndex` / `GetSkillLineIndexByID` — the modern
// skill-line view of the spellbook tabs.
//
// The engine's tab array (`Offsets::VAR_SPELL_TAB_ENTRIES`) stores each
// tab's SkillLine.dbc ID next to its spell count, but the stock Lua surface
// (`GetSpellTabInfo`) exposes only name / texture / offset / count — neither
// the ID nor the slot → tab direction was ever reachable from Lua. Both are
// direct reads of that array here. `GetSpellBookSkillLineInfo` builds the
// modern table from the same reads `Script_GetSpellTabInfo` makes.

#include "Tabs.h"

#include "Game.h"
#include "Lookup.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>

namespace Spell::Tabs {

namespace {

const uint8_t *TabEntry(int index0) {
    auto *entries = Game::Read<const uint8_t *const *>(
        Offsets::VAR_SPELL_TAB_ENTRIES);
    if (entries == nullptr)
        return nullptr;
    return entries[index0];
}

// `C_SpellBook.GetNumSpellBookSkillLines()` -> number.
int __fastcall Script_GetNumSpellBookSkillLines(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(Count()));
    return 1;
}

// `C_SpellBook.GetSpellBookSkillLineInfo(skillLineIndex)` -> table, or nil
// for an index outside `[1, Count()]`.
int __fastcall Script_GetSpellBookSkillLineInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (index < 1 || index > Count())
        return 0;

    const uint8_t *entry = TabEntry(index - 1);
    if (entry == nullptr)
        return 0;

    // Script_GetSpellTabInfo (0x004B3CE0): skill line 0 is the General tab,
    // the GENERAL global string with the Ability_Kick icon; any other skill
    // line gives its SkillLine.dbc name and SpellIcon.dbc path, nil for a
    // missing row.
    const char *name = nullptr;
    const char *icon = nullptr;
    const uint32_t skillLineID =
        Game::Read<uint32_t>(entry, Offsets::OFF_SPELL_TAB_SKILL_LINE_ID);
    if (skillLineID == 0) {
        name = Game::Lua::GlobalText("GENERAL");
        icon = "Interface\\Icons\\Ability_Kick";
    } else if (const uint8_t *skillLine = DBC::Record(Offsets::VAR_SKILL_LINE_RECORDS,
                                                      Offsets::VAR_SKILL_LINE_COUNT,
                                                      skillLineID)) {
        name = DBC::LocalizedString(skillLine, Offsets::OFF_SKILL_LINE_NAME);
        icon = DBC::StringField(Offsets::VAR_SPELL_ICON_RECORDS, Offsets::VAR_SPELL_ICON_COUNT,
                                Game::Read<uint32_t>(skillLine, Offsets::OFF_SKILL_LINE_SPELL_ICON),
                                Offsets::OFF_SPELLICON_PATH);
    }
    using SlotOffset_t = int(__fastcall *)(unsigned tabIndex0);
    const int offset = reinterpret_cast<SlotOffset_t>(
        static_cast<uintptr_t>(Offsets::FUN_SPELL_TAB_SLOT_OFFSET))(index - 1);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "name", name);
    // `iconID` is the texture PATH — the same deviation as
    // `C_SpellBook.GetSpellBookItemInfo`'s iconID (no fileIDs here).
    Game::Lua::SetFieldString(L, "iconID", icon);
    Game::Lua::SetFieldNumber(L, "itemIndexOffset", static_cast<double>(offset));
    Game::Lua::SetFieldNumber(
        L, "numSpellBookItems",
        static_cast<double>(Game::Read<uint32_t>(entry, Offsets::OFF_SPELL_TAB_NUM_SPELLS)));
    Game::Lua::SetFieldBool(L, "isGuild", false);
    Game::Lua::SetFieldBool(L, "shouldHide", false);
    // specID / offSpecID stay nil — no specializations.
    return 1;
}

// `C_SpellBook.GetSpellBookItemSkillLineIndex(slotIndex, spellBank)` ->
// number, or nil for a pet-book slot (pets have no skill lines) or a slot
// past the populated range.
int __fastcall Script_GetSpellBookItemSkillLineIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    if (Spell::Lookup::SpellBankArgToBookType(L, 2) != 0)
        return 0;
    const int index = IndexForSlot(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (index <= 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(index));
    return 1;
}

// `C_SpellBook.GetSkillLineIndexByID(skillLineID)` -> number, or nil when
// no tab carries that skill line.
int __fastcall Script_GetSkillLineIndexByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int index =
        IndexForSkillLine(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    if (index <= 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(index));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kNumSkillLinesRets[] = {
    Game::Doc::Req("numSpellBookSkillLines", "number"),
};
const Game::Doc::Function kGetNumSpellBookSkillLines{
    "Returns the number of tabs in the player spellbook, General included.",
    {}, kNumSkillLinesRets};

const Game::Doc::Field kSkillLineInfoFields[] = {
    Game::Doc::Req("name", "string"),
    Game::Doc::Req("iconID", "string", "Icon texture path; give it to texture:SetTexture."),
    Game::Doc::Req("itemIndexOffset", "number",
                   "This value + 1 is the first spellbook slot of the tab."),
    Game::Doc::Req("numSpellBookItems", "number"),
    Game::Doc::Req("isGuild", "bool", "Always false."),
    Game::Doc::Req("shouldHide", "bool", "Always false."),
    Game::Doc::Opt("specID", "number", nullptr, "Always nil."),
    Game::Doc::Opt("offSpecID", "number", nullptr, "Always nil."),
};
const Game::Doc::Structure kSkillLineInfo{"SpellBookSkillLineInfo", "SpellBook",
                                          kSkillLineInfoFields,
                                          "One tab of the player spellbook."};

const Game::Doc::Field kSkillLineInfoArgs[] = {
    Game::Doc::Req("skillLineIndex", "luaIndex"),
};
const Game::Doc::Field kSkillLineInfoRets[] = {
    Game::Doc::Opt("skillLineInfo", "SpellBookSkillLineInfo", nullptr,
                   "Nil for an index past the last tab."),
};
const Game::Doc::Function kGetSpellBookSkillLineInfo{
    "Returns a table describing one tab of the player spellbook.",
    kSkillLineInfoArgs, kSkillLineInfoRets};

const Game::Doc::Field kItemSkillLineArgs[] = {
    Game::Doc::Req("spellBookItemSlotIndex", "luaIndex"),
    Game::Doc::Opt("spellBookItemSpellBank", "SpellBookSpellBank", "0", "Player by default."),
};
const Game::Doc::Field kItemSkillLineRets[] = {
    Game::Doc::Opt("skillLineIndex", "luaIndex", nullptr,
                   "Nil for a slot past the last spell and for every pet-book slot."),
};
const Game::Doc::Function kGetSpellBookItemSkillLineIndex{
    "Returns the spellbook tab that holds a spellbook slot.",
    kItemSkillLineArgs, kItemSkillLineRets};

const Game::Doc::Field kIndexByIDArgs[] = {
    Game::Doc::Req("skillLineID", "number"),
};
const Game::Doc::Field kIndexByIDRets[] = {
    Game::Doc::Opt("skillIndex", "luaIndex", nullptr,
                   "Nil when no tab of yours belongs to that skill line."),
};
const Game::Doc::Function kGetSkillLineIndexByID{
    "Returns the spellbook tab for a skill line ID.", kIndexByIDArgs, kIndexByIDRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetNumSpellBookSkillLines",
                                     &Script_GetNumSpellBookSkillLines,
                                     &kGetNumSpellBookSkillLines);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSpellBookSkillLineInfo",
                                     &Script_GetSpellBookSkillLineInfo,
                                     &kGetSpellBookSkillLineInfo);
    Game::Lua::RegisterTableFunction("C_SpellBook",
                                     "GetSpellBookItemSkillLineIndex",
                                     &Script_GetSpellBookItemSkillLineIndex,
                                     &kGetSpellBookItemSkillLineIndex);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSkillLineIndexByID",
                                     &Script_GetSkillLineIndexByID, &kGetSkillLineIndexByID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

int Count() {
    return Game::Read<int>(Offsets::VAR_SPELL_TAB_COUNT);
}

int IndexForSlot(int slot1) {
    if (slot1 < 1)
        return 0;
    const int slot0 = slot1 - 1;
    const int count = Count();
    int offset = 0;
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = TabEntry(i);
        if (entry == nullptr)
            return 0;
        const int n = Game::Read<int>(entry, Offsets::OFF_SPELL_TAB_NUM_SPELLS);
        if (slot0 < offset + n)
            return i + 1;
        offset += n;
    }
    return 0;
}

int IndexForSkillLine(int skillLineID) {
    if (skillLineID <= 0)
        return 0;
    const int count = Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = TabEntry(i);
        if (entry != nullptr &&
            Game::Read<int>(entry, Offsets::OFF_SPELL_TAB_SKILL_LINE_ID) ==
                skillLineID)
            return i + 1;
    }
    return 0;
}

} // namespace Spell::Tabs
