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

// `C_Spell.SpellHasRange(spellIdentifier)` and the parallel
// `SpellHasRange(slot, bookType)` — true iff the spell's
// `SpellRange.dbc` row has a non-zero min or max range.
//
// Vanilla 1.12 doesn't ship `SpellHasRange` as a global at all; 3.3.5+
// added it but with the spellID-only signature. We provide the modern
// namespaced shape *and* a `(slot, bookType)` positional form mirroring
// the other dual-signature globals (`GetSpellInfo`, `GetSpellLink`,
// etc.) so callers can use whichever they already have.

#include "Arg.h"
#include "Lookup.h"
#include "Range.h"

#include "Game.h"

#include <cstring>

namespace Spell::HasRange {

static int __fastcall Script_C_Spell_SpellHasRange(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    Game::Lua::PushBool(L, Spell::Range::SpellIDHasRange(spellID));
    return 1;
}

// `SpellHasRange(slot, bookType)` — vanilla-style positional shape.
// `bookType` is the same string convention used by `GetSpellName`:
// `"spell"` (or omitted / anything-not-pet) → player book,
// `"pet"` → pet book.
static int __fastcall Script_SpellHasRange(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    int bookType = 0;
    if (Game::Lua::IsString(L, 2)) {
        const char *s = Game::Lua::ToString(L, 2);
        if (s != nullptr && _stricmp(s, "pet") == 0)
            bookType = 1;
    }
    const int spellID = Spell::Lookup::SpellbookSlotToID(slot, bookType);
    Game::Lua::PushBool(L, Spell::Range::SpellIDHasRange(spellID));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kByIdentifierArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kByIdentifierRets[] = {
    Game::Doc::Req("hasRange", "bool",
                   "True when the spell's minimum or maximum range is above zero."),
};
const Game::Doc::Function kC_SpellHasRange{
    "Whether the spell is cast at something at a distance rather than on the caster.",
    kByIdentifierArgs, kByIdentifierRets};

const Game::Doc::Field kBySlotArgs[] = {
    Game::Doc::Req("slot", "luaIndex", "A 1-based spellbook slot."),
    Game::Doc::Opt("bookType", "string", "\"spell\"",
                   "\"spell\" or \"pet\"; picks the book the slot belongs to."),
};
const Game::Doc::Field kBySlotRets[] = {
    Game::Doc::Req("hasRange", "bool",
                   "True when the spell's minimum or maximum range is above zero; "
                   "false for an empty slot."),
};
const Game::Doc::Function kSpellHasRange{
    "Whether the spellbook slot holds a spell you cast at a distance rather than on "
    "the caster.",
    kBySlotArgs, kBySlotRets, "SpellGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "SpellHasRange",
                                     &Script_C_Spell_SpellHasRange, &kC_SpellHasRange);
    Game::Lua::RegisterGlobalFunction("SpellHasRange",
                                      &Script_SpellHasRange, &kSpellHasRange);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::HasRange
