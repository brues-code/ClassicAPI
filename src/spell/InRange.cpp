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
// Thin Lua wrapper over the shared `Spell::Range::PlayerVsUnit` core
// (see spell/Range.h) — the same range test behind `C_Item.IsItemInRange`
// (via the item's on-use spell). Range-only, ignoring line of sight and
// target faction, matching retail IsSpellInRange.

#include "Arg.h"
#include "Range.h"

#include "Game.h"

namespace Spell::InRange {

namespace {

int __fastcall Script_C_Spell_IsSpellInRange(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const char *unit =
        Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;

    const int result = Spell::Range::PlayerVsUnit(spellID, unit);
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
                   "True when in range, false when out of range, nil when the spell or "
                   "unit gives no answer."),
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
