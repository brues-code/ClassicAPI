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

// `C_Spell.GetSpellEffectMechanics(spellID)` -> { m1, m2, m3 }
//
// Returns a spell's three per-effect mechanic ids (Spell.dbc
// `EffectMechanic[3]`) as a 1-based array table, or nil for an invalid /
// out-of-range spell ID. Each entry is a standard WoW SpellMechanic id
// (0 = that effect carries no mechanic); the numbering matches
// C_Spell.GetSpellMechanicByID (1 = Charm, 7 = Root, 12 = Stun,
// 15 = Bleed, 17 = Polymorph, ...).
//
// Complements GetSpellMechanicByID, which reads only the SPELL-level
// `Mechanic` field (+0x14). Vanilla frequently stores the meaningful
// mechanic on an *effect* rather than the spell — periodic damage such
// as bleeds is the common case — so effect-mechanic-aware callers (e.g.
// bleed classification for immunity tracking) need this array too:
//
//   local em = C_Spell.GetSpellEffectMechanics(spellID)
//   if em then for i = 1, 3 do if em[i] == 15 then --[[ bleed ]] end end end
//
// Reads Spell.dbc directly, so it covers every spell the client knows —
// not just the player's spellbook — with no caching or network round-trip
// (Spell.dbc is resident from boot).

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::EffectMechanic {

namespace {

int __fastcall Script_GetSpellEffectMechanics(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0; // nil for invalid / out-of-range spell IDs

    auto *mechanics = reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_MECHANIC);

    // Fixed 1-based array of all three effects (0 = effect has no mechanic),
    // matching the raw EffectMechanic[3] layout so callers can index by slot.
    Game::Lua::NewTable(L);
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));        // key
        Game::Lua::PushNumber(L, static_cast<double>(mechanics[i])); // value
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kGetSpellEffectMechanicsArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kGetSpellEffectMechanicsRets[] = {
    Game::Doc::Opt("mechanics", "table", nullptr,
                   "Three mechanic IDs, one per effect slot, counted from 1; 0 where an "
                   "effect carries no mechanic. Nil for an unknown spell."),
};
const Game::Doc::Function kGetSpellEffectMechanics{
    "The mechanic each of the spell's three effects applies, such as root or bleed.",
    kGetSpellEffectMechanicsArgs, kGetSpellEffectMechanicsRets};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellEffectMechanics",
                                     &Script_GetSpellEffectMechanics,
                                     &kGetSpellEffectMechanics);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::EffectMechanic
