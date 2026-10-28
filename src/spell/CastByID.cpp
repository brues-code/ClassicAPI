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

// `CastSpellByID(spellID[, target])` — casts the exact spell ID (that rank,
// from the player's spellbook or the pet's) at the current target, or at the
// unit `target` names.
//
// 4.3.4 (Script_CastSpellByID) looks the ID up in the player's spellbook and
// then the pet's, and hands it to the cast path with the target's GUID. Here
// that is `Spell::Placement::DispatchSpellCast`: the same two-book lookup by
// exact ID into the engine's cast dispatcher, which substitutes the GUID where
// it would the current selection. A ground-target spell enters placement as
// it does when cast by hand. Like 4.3.4, a target token that names no unit
// and a spell neither book holds return nothing; a dispatched cast returns
// true.

#include "Game.h"
#include "spell/MacroPrimarySpell.h"
#include "spell/Placement.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Spell::CastByID {

namespace {

int __fastcall Script_CastSpellByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: CastSpellByID(spellID[, target])");
        return 0;
    }
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (spellID == 0)
        return 0;

    uint64_t targetGuid = 0;
    if (Game::Lua::IsString(L, 2)) {
        const char *token = Game::Lua::ToString(L, 2);
        if (token != nullptr && *token != '\0') {
            targetGuid = Unit::Identity::GuidForToken(token);
            if (targetGuid == 0)
                return 0;
        }
    }

    if (!Spell::Placement::DispatchSpellCast(spellID, targetGuid))
        return 0;
    Game::Lua::PushBool(L, true);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spellID", "number", "The exact spell to cast, rank included."),
    Game::Doc::Opt("target", "UnitToken", nullptr,
                   "The unit to cast at; without it the spell goes to your current target."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("cast", "bool", nullptr,
                   "True when the spell went to the cast path, which can still refuse "
                   "it (range, cooldown); nil when neither you nor your pet knows the "
                   "spell, or the target unit does not exist."),
};
const Game::Doc::Function kCastSpellByID{
    "Casts a spell from your or your pet's spellbook by its spell ID.", kArgs, kRets,
    "SpellGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("CastSpellByID", &Script_CastSpellByID,
                                      &kCastSpellByID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Tag macros calling `CastSpellByID(<id>)` with the spellID so action-bar
// UIs highlight the slot, as `CastSpellByName` macros are.
const Spell::MacroPrimarySpell::PatternAutoRegister _patreg{
    "CastSpellByID(", &Spell::MacroPrimarySpell::ExtractSpellArg};

} // namespace

} // namespace Spell::CastByID
