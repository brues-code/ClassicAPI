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

// Where a spell sits on the bars.
//
//   FindSpellActionButtons(spell [, set])  the 1-based slots holding the
//       spell as a spell action, or nothing when none does.
//   HasSpellActionButtons(spell [, set])   whether any slot does.
//   IsOnBarOrSpecialBar(spell [, set])     the same, or the spell is on the
//       shapeshift bar or the pet bar.
//
// A slot matches on the spell ID it stores, as 4.3.4's PutActionInSlot
// compares (IsSpell && GetSpell == spellId): a macro or an item that casts
// the spell is not the spell, and a rank matches only itself. The special
// bars are the engine's own lists — the shapeshift bar's spell array that
// GetShapeshiftFormInfo reads, and the pet bar's spell entries
// (GetPetActionInfo's types 1-5) while a pet bar is up.
//
// `set` is Enum.ActionBarSet, a mask whose Mkb bit is the one set of slots
// this client has; Gamepad alone finds no slot.

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"

#include <cstdint>

namespace Action::Find {

namespace {

constexpr int ACTION_BAR_SET_MKB = 1;
constexpr int ACTION_BAR_SET_GAMEPAD = 2;
constexpr int ACTION_BAR_SET_ALL = ACTION_BAR_SET_MKB | ACTION_BAR_SET_GAMEPAD;

// The spell at stack[1] and whether the set at stack[2] (default All)
// covers this client's slots. 0 when the spell does not resolve.
int Args(void *L, bool *searchSlots) {
    const int set = Game::Lua::IsNumber(L, 2) ? static_cast<int>(Game::Lua::ToNumber(L, 2))
                                              : ACTION_BAR_SET_ALL;
    *searchSlots = (set & ACTION_BAR_SET_MKB) != 0;
    return Spell::Arg::ResolveSpellID(L, 1);
}

bool SlotHolds(int slot0, int spellID) {
    const uint32_t entry = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ACTION_TABLE))[slot0];
    return (entry & Offsets::ACTION_TYPE_MASK) == Offsets::ACTION_TYPE_SPELL &&
           entry == static_cast<uint32_t>(spellID);
}

bool AnySlotHolds(int spellID) {
    for (int slot0 = 0; slot0 < Offsets::ACTION_TABLE_MAX_SLOTS; ++slot0) {
        if (SlotHolds(slot0, spellID))
            return true;
    }
    return false;
}

bool OnShapeshiftBar(int spellID) {
    const int count = Game::Read<int>(Offsets::VAR_SHAPESHIFT_BAR_SPELL_COUNT);
    const auto *spells = Game::Read<const uint32_t *>(Offsets::VAR_SHAPESHIFT_BAR_SPELLS);
    if (spells == nullptr)
        return false;
    for (int i = 0; i < count; ++i) {
        if (spells[i] == static_cast<uint32_t>(spellID))
            return true;
    }
    return false;
}

bool OnPetBar(int spellID) {
    if (Game::Read<uint64_t>(Offsets::VAR_PET_GUID) == 0)
        return false;
    const auto *slots = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PET_ACTION_SLOTS));
    for (int i = 0; i < Offsets::PET_ACTION_SLOT_COUNT; ++i) {
        const uint32_t type = (slots[i] >> 24) & 0x3F;
        if (type >= Offsets::PET_ACTION_TYPE_SPELL_FIRST &&
            type <= Offsets::PET_ACTION_TYPE_SPELL_LAST &&
            (slots[i] & 0xFFFF) == static_cast<uint32_t>(spellID))
            return true;
    }
    return false;
}

int __fastcall Script_FindSpellActionButtons(void *L) {
    bool searchSlots;
    const int spellID = Args(L, &searchSlots);
    if (spellID <= 0 || !searchSlots || !AnySlotHolds(spellID))
        return 0;
    Game::Lua::NewTable(L);
    int index = 0;
    for (int slot0 = 0; slot0 < Offsets::ACTION_TABLE_MAX_SLOTS; ++slot0) {
        if (!SlotHolds(slot0, spellID))
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(++index));
        Game::Lua::PushNumber(L, static_cast<double>(slot0 + 1));
        Game::Lua::RawSet(L, -3);
    }
    return 1;
}

int __fastcall Script_HasSpellActionButtons(void *L) {
    bool searchSlots;
    const int spellID = Args(L, &searchSlots);
    Game::Lua::PushBool(L, spellID > 0 && searchSlots && AnySlotHolds(spellID));
    return 1;
}

int __fastcall Script_IsOnBarOrSpecialBar(void *L) {
    bool searchSlots;
    const int spellID = Args(L, &searchSlots);
    Game::Lua::PushBool(L, spellID > 0 && ((searchSlots && AnySlotHolds(spellID)) ||
                                           OnShapeshiftBar(spellID) || OnPetBar(spellID)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spellID", "SpellIdentifier", "A spell ID, spell link, or spell name."),
    Game::Doc::Opt("setToSearch", "ActionBarSet", "All"),
};
const Game::Doc::Field kFindRets[] = {
    Game::Doc::Req("slots", "table", "The 1-based action slots; nothing when no slot "
                                     "holds the spell."),
};
const Game::Doc::Function kFindSpellActionButtons{
    "The action slots that hold the spell as a spell action.",
    kArgs, kFindRets};

const Game::Doc::Field kHasRets[] = {
    Game::Doc::Req("hasSpellActionButtons", "bool"),
};
const Game::Doc::Function kHasSpellActionButtons{
    "Whether any action slot holds the spell as a spell action.",
    kArgs, kHasRets};

const Game::Doc::Field kOnBarRets[] = {
    Game::Doc::Req("isOnBarOrSpecialBar", "bool"),
};
const Game::Doc::Function kIsOnBarOrSpecialBar{
    "Whether the spell is in an action slot, on the shapeshift bar, or on the pet bar.",
    kArgs, kOnBarRets};

const Game::Lua::EnumIntegerEntry kActionBarSet[] = {
    {"None", 0},
    {"Mkb", ACTION_BAR_SET_MKB},
    {"Gamepad", ACTION_BAR_SET_GAMEPAD},
    {"All", ACTION_BAR_SET_ALL},
};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_ActionBar", "FindSpellActionButtons",
                                     &Script_FindSpellActionButtons, &kFindSpellActionButtons);
    Game::Lua::RegisterTableFunction("C_ActionBar", "HasSpellActionButtons",
                                     &Script_HasSpellActionButtons, &kHasSpellActionButtons);
    Game::Lua::RegisterTableFunction("C_ActionBar", "IsOnBarOrSpecialBar",
                                     &Script_IsOnBarOrSpecialBar, &kIsOnBarOrSpecialBar);
    Game::Lua::RegisterIntegerEnum("Enum", "ActionBarSet", kActionBarSet, "ActionBar");
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Action::Find
