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

// `C_Spell.DoesSpellExist(spellID)` — true iff the spell ID resolves
// to a populated `Spell.dbc` record. Cheap existence probe useful
// before calling other `C_Spell.*` functions that might OOB-crash or
// return nil/empty for missing spells.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>

namespace Spell::Exists {

static int __fastcall Script_DoesSpellExist(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (spellID <= 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uint8_t *rec = DBC::Record(Offsets::VAR_SPELL_RECORDS,
                                     Offsets::VAR_SPELL_RECORD_COUNT,
                                     static_cast<uint32_t>(spellID));
    Game::Lua::PushBool(L, rec != nullptr);
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kDoesSpellExistArgs[] = {
    Game::Doc::Req("spellID", "number", "The spell ID to test."),
};
static const Game::Doc::Field kDoesSpellExistRets[] = {
    Game::Doc::Req("exists", "bool", "True when the client holds data for that spell ID."),
};
static const Game::Doc::Function kDoesSpellExist{
    "Whether a spell ID names a real spell, as a check before you read more about it.",
    kDoesSpellExistArgs, kDoesSpellExistRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "DoesSpellExist",
                                     &Script_DoesSpellExist, &kDoesSpellExist);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Exists
