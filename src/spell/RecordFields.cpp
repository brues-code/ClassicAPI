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
// Melee rules (server core, Spell.h / Spell.cpp):
//   on-next-swing = Attributes & (0x04 | 0x400)  — two bits (Spell.h:434)
//   resets swing  = (InterruptFlags & 0x08) && !(AttributesEx2 & 0x20000)
//                   (Spell.cpp:3964 + Spell.h:441)

#include "Arg.h"
#include "Lookup.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::RecordFields {

namespace {

// Attributes bits 0x04 (SPELL_ATTR_ON_NEXT_SWING) and 0x400
// (SPELL_ATTR_ON_NEXT_SWING_2) both mark an on-next-melee-swing ability
// (server Spell.h:434 tests the pair).
constexpr uint32_t SPELL_ATTR_ON_NEXT_SWING = 0x04 | 0x400;
// InterruptFlags bit 0x08 = SPELL_INTERRUPT_FLAG_AUTOATTACK.
constexpr uint32_t SPELL_INTERRUPT_AUTOATTACK = 0x08;
// AttributesEx2 bit 0x20000 = NOT_RESET_AUTO_ACTIONS — exempts the spell
// from the swing reset the AUTOATTACK interrupt flag would otherwise cause.
constexpr uint32_t SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS = 0x20000;

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
// (Heroic Strike, Maul, Cleave, Raptor Strike, Slam).
static int __fastcall Script_IsNextMeleeSpell(void *L) {
    const uint8_t *rec = Spell::Lookup::RecordForID(Spell::Arg::ResolveSpellID(L, 1));
    const bool onNextSwing =
        rec != nullptr &&
        (ReadField(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) & SPELL_ATTR_ON_NEXT_SWING) != 0;
    Game::Lua::PushBool(L, onNextSwing);
    return 1;
}

// C_Spell.ResetsMeleeSwing(spellId) -> bool. True when casting the spell
// resets the melee swing timer (AUTOATTACK interrupt flag set and the
// NOT_RESET_AUTO_ACTIONS exemption clear).
static int __fastcall Script_ResetsMeleeSwing(void *L) {
    const uint8_t *rec = Spell::Lookup::RecordForID(Spell::Arg::ResolveSpellID(L, 1));
    const bool resets =
        rec != nullptr &&
        (ReadField(rec, Offsets::OFF_SPELL_RECORD_INTERRUPT_FLAGS) &
         SPELL_INTERRUPT_AUTOATTACK) != 0 &&
        (ReadField(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX2) &
         SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS) == 0;
    Game::Lua::PushBool(L, resets);
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
    "Which kind of dispel removes the effects of a spell.", kSpellArgs, kDispelTypeRets};

const Game::Doc::Field kNextMeleeRets[] = {
    Game::Doc::Req("isNextMelee", "bool",
                   "True when the ability lands on your next weapon swing."),
};
const Game::Doc::Function kIsNextMeleeSpell{
    "Whether the ability waits for your next weapon swing instead of casting.",
    kSpellArgs, kNextMeleeRets};

const Game::Doc::Field kResetsSwingRets[] = {
    Game::Doc::Req("resetsSwing", "bool",
                   "True when casting the spell starts the swing timer again."),
};
const Game::Doc::Function kResetsMeleeSwing{
    "Whether casting the spell starts your melee swing timer again.",
    kSpellArgs, kResetsSwingRets};

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
