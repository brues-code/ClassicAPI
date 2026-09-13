// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `C_Spell.GetSpellReagents(spellIdentifier)` — modern accessor for
// a spell's reagent list. Returns an array of `{itemID, count}`
// tables, one per non-empty reagent slot. Reagents apply mostly to
// crafting recipes and a few class spells (Soulshatter / Soulstone
// items, paladin Symbol of Kings, etc.).
//
// Reads directly out of `Spell.dbc` at the documented offsets:
//   - `+0x110`: int32 itemID[8]
//   - `+0x130`: int32 count[8]
// Empty slots have itemID == 0; iteration stops at the first empty
// slot, matching how the engine itself walks the reagent table
// (`Script_GetTradeSkillReagentInfo` etc.).

#include "Arg.h"
#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::Reagents {

namespace {

int __fastcall Script_C_Spell_GetSpellReagents(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0)
        return 0;
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0;

    auto *ids = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_REAGENT_ID);
    auto *counts = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_REAGENT_COUNT);

    Game::Lua::NewTable(L); // outer array
    int nextKey = 1;
    for (int i = 0; i < Offsets::SPELL_RECIPE_MAX_REAGENTS; ++i) {
        if (ids[i] == 0)
            break; // engine convention: stop at first empty slot
        Game::Lua::PushNumber(L, static_cast<double>(nextKey++));
        Game::Lua::NewTable(L); // {itemID = ?, count = ?}
        Game::Lua::SetFieldNumber(L, "itemID", static_cast<double>(ids[i]));
        Game::Lua::SetFieldNumber(L, "count", static_cast<double>(counts[i]));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSpellReagentInfoFields[] = {
    Game::Doc::Req("itemID", "number", "The item the cast consumes."),
    Game::Doc::Req("count", "number", "How many of that item one cast consumes."),
};
const Game::Doc::Structure kSpellReagentInfo{
    "SpellReagentInfo", "Spell", kSpellReagentInfoFields,
    "One item a spell consumes when you cast it."};

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("reagents", "table", nullptr,
                   "An array of SpellReagentInfo entries, empty when the spell uses "
                   "none; nil for an unknown spell."),
};
const Game::Doc::Function kGetSpellReagents{
    "The items a spell consumes each time you cast it.", kArgs, kRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellReagents",
                                     &Script_C_Spell_GetSpellReagents,
                                     &kGetSpellReagents);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Reagents
