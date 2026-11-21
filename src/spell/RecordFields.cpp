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

// `C_Spell.GetSpellDispelType` / `IsNextMeleeSpell` / `ResetsMeleeSwing` —
// read raw Spell.dbc fields straight off the record so pfUI can drop its
// nampower `GetSpellRecField` dependency.
//
// The Spell.dbc record fields we read live in `Offsets.h` (the single source
// of truth for the record layout): `OFF_SPELL_DISPEL_TYPE` (+0x10),
// `OFF_SPELL_RECORD_ATTRIBUTES` (+0x18), `OFF_SPELL_RECORD_ATTRIBUTES_EX2`
// (+0x20), `OFF_SPELL_RECORD_INTERRUPT_FLAGS` (+0x54).
//
// The two melee answers are Combat::Swing's rules, the ones the swing timer
// itself runs on, so the API and PLAYER_SWING never disagree (a server
// override such as Turtle's Slam reset included).

#include "Arg.h"
#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"
#include "combat/Swing.h"

#include <cstdint>

namespace Spell::RecordFields {

namespace {

uint32_t ReadField(const uint8_t *rec, int offset) {
    return *reinterpret_cast<const uint32_t *>(rec + offset);
}

} // namespace

// C_Spell.GetSpellDispelType(spellId) -> number. 0 for a non-dispellable
// spell or an unknown ID.
static int __fastcall Script_GetSpellDispelType(void *L) {
    const uint8_t *rec = Spell::Lookup::RecordForID(Spell::Arg::ResolveSpellID(L, 1));
    Game::Lua::PushNumber(
        L, rec != nullptr
               ? static_cast<double>(ReadField(rec, Offsets::OFF_SPELL_DISPEL_TYPE))
               : 0.0);
    return 1;
}

// C_Spell.IsNextMeleeSpell(spellId) -> bool. True for on-next-swing abilities
// (Heroic Strike, Maul, Cleave, Raptor Strike).
static int __fastcall Script_IsNextMeleeSpell(void *L) {
    Game::Lua::PushBool(L, Combat::Swing::IsNextSwing(
                               Spell::Lookup::RecordForID(Spell::Arg::ResolveSpellID(L, 1))));
    return 1;
}

// C_Spell.ResetsMeleeSwing(spellId) -> bool. True when casting the spell
// resets the melee swing timer.
static int __fastcall Script_ResetsMeleeSwing(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    Game::Lua::PushBool(L, Combat::Swing::ResetsSwing(static_cast<uint32_t>(spellID),
                                                      Spell::Lookup::RecordForID(spellID)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kSpellArgs[] = {
    Game::Doc::Req("spellID", "SpellIdentifier",
                   "A spell ID, spell link, or spell name."),
};

const Game::Doc::Field kDispelTypeRets[] = {
    Game::Doc::Req("dispelType", "number",
                   "1 magic, 2 curse, 3 disease, 4 poison; 0 when nothing removes it."),
};
const Game::Doc::Function kGetSpellDispelType{
    "Returns which kind of dispel removes the effects of a spell.", kSpellArgs, kDispelTypeRets,
    nullptr, true};

const Game::Doc::Field kNextMeleeRets[] = {
    Game::Doc::Req("isNextMelee", "bool",
                   "True when the ability lands on your next weapon swing."),
};
const Game::Doc::Function kIsNextMeleeSpell{
    "Returns whether the ability waits for your next weapon swing instead of casting.",
    kSpellArgs, kNextMeleeRets, nullptr, true};

const Game::Doc::Field kResetsSwingRets[] = {
    Game::Doc::Req("resetsSwing", "bool",
                   "True when casting the spell starts the swing timer again."),
};
const Game::Doc::Function kResetsMeleeSwing{
    "Returns whether casting the spell starts your melee swing timer again.",
    kSpellArgs, kResetsSwingRets, nullptr, true};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellDispelType",
                                     &Script_GetSpellDispelType, &kGetSpellDispelType);
    Game::Lua::RegisterTableFunction("C_Spell", "IsNextMeleeSpell",
                                     &Script_IsNextMeleeSpell, &kIsNextMeleeSpell);
    Game::Lua::RegisterTableFunction("C_Spell", "ResetsMeleeSwing",
                                     &Script_ResetsMeleeSwing, &kResetsMeleeSwing);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::RecordFields
