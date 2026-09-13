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

// `C_Spell.GetSpellSubtext(spellIdentifier)` — the localized "Rank N"
// / "Passive" / "Racial Passive" string from `Spell.dbc.Rank[9]`.
// String-keyed input resolves by name through the spellbook (modern
// `SpellIdentifier` semantics); numeric input is a direct spellID
// lookup.

#include "Arg.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>

namespace Spell::Subtext {

namespace {

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("subtext", "string", nullptr,
                   "The rank line, nil when the spell has none."),
};
const Game::Doc::Function kGetSpellSubtext{
    "The rank text shown under a spell's name in the spellbook.", kArgs, kRets};

} // namespace

static int __fastcall Script_GetSpellSubtext(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const char *subtext = DBC::LocalizedField(Offsets::VAR_SPELL_RECORDS,
                                              Offsets::VAR_SPELL_RECORD_COUNT,
                                              static_cast<uint32_t>(spellID),
                                              Offsets::OFF_SPELL_RECORD_RANK);
    if (subtext == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, subtext);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellSubtext",
                                     &Script_GetSpellSubtext, &kGetSpellSubtext);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Subtext
