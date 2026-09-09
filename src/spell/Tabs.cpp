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
// direct reads of that array here. `GetSpellBookSkillLineInfo` calls the
// engine's own `Script_GetSpellTabInfo` for the name / icon so the
// GENERAL-tab localization and the SkillLine → SpellIcon chain stay the
// engine's, then reshapes its four returns into the modern table.

#include "Tabs.h"

#include "Game.h"
#include "Lookup.h"
#include "Offsets.h"

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

using ScriptFn_t = int(__fastcall *)(void *L);

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

    // The engine function reads L[1] (our index) and pushes
    // `name, texture, offset, numSpells`. Stack afterwards:
    // [index, name, texture, offset, numSpells].
    Game::Lua::SetTop(L, 1);
    reinterpret_cast<ScriptFn_t>(
        static_cast<uintptr_t>(Offsets::FUN_SCRIPT_GET_SPELL_TAB_INFO))(L);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "name", Game::Lua::ToString(L, 2));
    // `iconID` is the texture PATH — the same deviation as
    // `C_SpellBook.GetSpellBookItemInfo`'s iconID (no fileIDs here).
    Game::Lua::SetFieldString(L, "iconID", Game::Lua::ToString(L, 3));
    Game::Lua::SetFieldNumber(L, "itemIndexOffset", Game::Lua::ToNumber(L, 4));
    Game::Lua::SetFieldNumber(L, "numSpellBookItems",
                              Game::Lua::ToNumber(L, 5));
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

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetNumSpellBookSkillLines",
                                     &Script_GetNumSpellBookSkillLines);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSpellBookSkillLineInfo",
                                     &Script_GetSpellBookSkillLineInfo);
    Game::Lua::RegisterTableFunction("C_SpellBook",
                                     "GetSpellBookItemSkillLineIndex",
                                     &Script_GetSpellBookItemSkillLineIndex);
    Game::Lua::RegisterTableFunction("C_SpellBook", "GetSkillLineIndexByID",
                                     &Script_GetSkillLineIndexByID);
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
