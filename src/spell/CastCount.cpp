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

// `C_Spell.GetSpellCastCount(spellIdentifier)` /
// `C_SpellBook.GetSpellBookItemCastCount(slotIndex, spellBank)` — how many
// times a spell can be cast with the reagents the player carries.
//
// 1.12 has no reagent-uses helper (3.3.5's `Spell_C_GetSpellReagentUses`),
// and its action-button count (`Script_GetActionCount` → 0x004E6C70) is
// ITEM-only. 3.3.5's helper is the engine's own cast gate restated as a
// count, and 1.12 has that gate whole: `CheckSpellTotemsAndReagents`
// (0x006E4000) walks the 8 reagent slots, skips empty ones, and compares
// `FUN_INVMGR_COUNT_STACKS_BY_ID(invMgr, reagent, 0)` — no bank — against
// the required count. The count is the minimum over those slots of
// floor(carried / required). Spells with no reagents return 0, matching
// retail's `GetSpellCastCount`.

#include "CastCount.h"

#include "Arg.h"
#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Count.h"

#include <cstdint>

namespace Spell::CastCount {

int ForSpell(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;
    auto *ids = Game::Ptr<const int32_t>(record, Offsets::OFF_SPELL_REAGENT_ID);
    auto *counts =
        Game::Ptr<const int32_t>(record, Offsets::OFF_SPELL_REAGENT_COUNT);
    int casts = -1;
    for (int i = 0; i < Offsets::SPELL_MAX_REAGENTS; ++i) {
        if (ids[i] == 0 || counts[i] <= 0)
            continue;
        const int possible = Item::Count::InInventory(ids[i]) / counts[i];
        if (casts < 0 || possible < casts)
            casts = possible;
    }
    return casts < 0 ? 0 : casts;
}

namespace {

int __fastcall Script_C_Spell_GetSpellCastCount(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const int casts = ForSpell(spellID);
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(casts));
    return 1;
}

int __fastcall Script_C_SpellBook_GetSpellBookItemCastCount(void *L) {
    const int spellID = Spell::Lookup::SpellbookItemArgsToID(L, 1, 2);
    const int casts = ForSpell(spellID);
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, static_cast<double>(casts));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kBySpellArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kBySpellRets[] = {
    Game::Doc::Req("castCount", "number",
                   "Casts the carried reagents allow; 0 for an unknown spell and for a "
                   "spell that needs no reagents."),
};
const Game::Doc::Function kGetSpellCastCount{
    "How many times you can cast the spell with the reagents you carry.",
    kBySpellArgs, kBySpellRets};

const Game::Doc::Field kBySlotArgs[] = {
    Game::Doc::Req("slotIndex", "luaIndex", "A spellbook slot, counted from 1."),
    Game::Doc::Opt("spellBank", "number", "0", "1 reads the pet book; anything else "
                                               "reads the player book."),
};
const Game::Doc::Field kBySlotRets[] = {
    Game::Doc::Req("castCount", "number",
                   "Casts the carried reagents allow; 0 for an empty slot and for a "
                   "spell that needs no reagents."),
};
const Game::Doc::Function kGetSpellBookItemCastCount{
    "How many times you can cast the slot's spell with the reagents you carry.",
    kBySlotArgs, kBySlotRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellCastCount",
                                     &Script_C_Spell_GetSpellCastCount,
                                     &kGetSpellCastCount);
    Game::Lua::RegisterTableFunction(
        "C_SpellBook", "GetSpellBookItemCastCount",
        &Script_C_SpellBook_GetSpellBookItemCastCount, &kGetSpellBookItemCastCount);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::CastCount
