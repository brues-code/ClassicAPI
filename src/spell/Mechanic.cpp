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

// `C_Spell.GetSpellMechanicByID(spellID)` -> (mechanicID, localizedName)
//
// Reads the spell-level Mechanic field straight out of Spell.dbc, so it
// covers *every* spell the client knows — not just the player's
// spellbook — with no caching or network round-trip (Spell.dbc is fully
// resident from boot). This replaces hand-maintained spellID -> mechanic
// tables (e.g. SuperCleveRoidMacros' `CCSpellMechanics`) that addons keep
// solely because vanilla exposes no Lua reader for the field.
//
// Returns:
//   - nothing (nil) for an invalid / out-of-range spell ID
//   - (0, nil) for a known spell that carries no mechanic
//   - (mechanicID, name) otherwise. `name` is the enUS SpellMechanic.dbc
//     name (always English, locale-independent — it's a stable token
//     consumers match against). nil only if the mechanic has no record /
//     no English name; the numeric ID is still returned in that case.
//
// The mechanic numbering is WoW's standard SpellMechanic enum
// (1 = Charm, 5 = Fear, 7 = Root, 12 = Stun, 17 = Polymorph, ...);
// addons map those IDs to their own names as needed.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Mechanic {

// SpellMechanic.dbc name — always the enUS (index 0) column, regardless
// of client locale. The mechanic name is a stable token consumers key
// off (`"rooted"`, `"stunned"`, …), so it must be identical on every
// client; a localized value would break cross-locale string matching.
// Vanilla only populates the enUS column of this table anyway. Returns
// nullptr if the record is missing or the English slot is empty.
static const char *MechanicName(uint32_t mechanicID) {
    const uint8_t *rec = DBC::Record(Offsets::VAR_SPELLMECHANIC_RECORDS,
                                     Offsets::VAR_SPELLMECHANIC_COUNT, mechanicID);
    if (rec == nullptr)
        return nullptr;
    auto names = Game::Ptr<const char *const>(rec, Offsets::OFF_SPELLMECHANIC_NAME);
    const char *s = names[0]; // enUS
    return (s != nullptr && s[0] != '\0') ? s : nullptr;
}

static int __fastcall Script_GetSpellMechanicByID(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0; // nil for invalid / out-of-range spell IDs

    const uint32_t mechanicID =
        Game::Read<uint32_t>(record, Offsets::OFF_SPELL_RECORD_MECHANIC);

    Game::Lua::PushNumber(L, static_cast<double>(mechanicID));

    if (mechanicID == 0) {
        Game::Lua::PushNil(L); // known spell, no mechanic
        return 2;
    }

    // Locale name with enUS fallback (nullptr → nil if even English is
    // empty); the caller still gets the numeric ID either way.
    Game::Lua::PushString(L, MechanicName(mechanicID)); // pushstring(NULL) → pushnil
    return 2;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spellID", "SpellIdentifier",
                   "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("mechanicID", "number", nullptr,
                   "The mechanic the spell applies, 0 when it applies none; "
                   "nil for an unknown spell."),
    Game::Doc::Opt("name", "string", nullptr,
                   "The English mechanic name, nil when the mechanic has none."),
};
const Game::Doc::Function kGetSpellMechanicByID{
    "The crowd-control mechanic a spell applies, with its English name.",
    kArgs, kRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellMechanicByID",
                                     &Script_GetSpellMechanicByID,
                                     &kGetSpellMechanicByID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Mechanic
