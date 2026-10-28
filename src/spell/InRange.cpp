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

// `C_Spell.IsSpellInRange(spellIdentifier, targetUnit)` — true if the
// spell is in range of the unit, false if out of range, nil when the
// range check doesn't apply (rangeless spell, or an unresolvable
// spell / unit). `spellIdentifier` is a spellID, spell link, or the
// name of a spell in the player's spellbook; `targetUnit` is a unit
// token ("target", "mouseover", "party1", …).
//
// Thin Lua wrapper over `Spell::Range::CastVsUnit` (see spell/Range.h),
// 4.3.4's Spell_C_RangeCheckSelected: nil also for a unit the spell cannot
// be cast at. The same check is behind `C_Item.IsItemInRange` (via the
// item's on-use spell). Like 4.3.4's IsSpellInRange, a pet-book spell is
// measured from the pet. A name takes the book the engine's name resolver
// matched it in; an ID takes the pet when the pet book holds it and the
// player's does not (FindSpellbookSlot searches the player's book first).

#include "Arg.h"
#include "Range.h"

#include "Game.h"
#include "spell/Lookup.h"

namespace Spell::InRange {

namespace {

int __fastcall Script_C_Spell_IsSpellInRange(void *L) {
    const Spell::Arg::Resolved arg = Spell::Arg::Resolve(L, 1);
    int isPet = 0;
    int spellID = arg.spellID;
    if (spellID > 0) {
        int book = 0;
        isPet = Spell::Lookup::FindSpellbookSlot(spellID, &book) != 0 && book == 1;
    } else if (arg.name != nullptr) {
        spellID = Spell::Arg::NameToSpellID(arg.name, &isPet);
    }
    const char *unit =
        Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;

    const int result = Spell::Range::CastVsUnit(spellID, unit, isPet != 0);
    if (result < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushBool(L, result == 1);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
    Game::Doc::Req("targetUnit", "UnitToken", "The unit to measure the distance to."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("inRange", "bool", nullptr,
                   "True when in range, false when out of range, nil when the spell "
                   "cannot be cast at the unit."),
};
const Game::Doc::Function kIsSpellInRange{
    "Whether the player is close enough to the unit to cast the spell on it.",
    kArgs, kRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellInRange",
                                     &Script_C_Spell_IsSpellInRange, &kIsSpellInRange);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::InRange
