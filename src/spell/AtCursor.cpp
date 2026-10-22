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

// `C_Spell.CastAtCursor(spellIDOrName)` — cast a ground-target spell at
// the player's current cursor world position, bypassing the manual
// click on the AoE reticle.
//
//   1. Cancel any placement already up, so the one committed below is
//      the one this call armed (see Placement.h).
//   2. Dispatch the cast (`Spell::Placement::DispatchSpellCast[ByName]`).
//      A ground-target spell enters placement mode; a normal cast fires
//      immediately.
//   3. `Spell::Placement::CommitAtCursor()` binds the placement at the
//      cursor's terrain hit, as the engine's own ground click does.
//
// Returns whether the cast went out at the cursor. The cast itself
// proceeds either way for a non-ground spell.

#include "Game.h"
#include "spell/MacroPrimarySpell.h"
#include "spell/Placement.h"

namespace Spell::AtCursor {

namespace {

int __fastcall Script_C_Spell_CastAtCursor(void *L) {
    // Accept a numeric spellID (exact rank) or a spell name (the engine's
    // resolver parses a trailing "(Rank N)"). IsNumber is checked first so a
    // numeric string like "10" still takes the exact-spellID path.
    const bool byNumber = Game::Lua::IsNumber(L, 1);
    if (!byNumber && !Game::Lua::IsString(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    Spell::Placement::Stop();
    const bool dispatched =
        byNumber ? Spell::Placement::DispatchSpellCast(
                       static_cast<int>(Game::Lua::ToNumber(L, 1)))
                 : Spell::Placement::DispatchSpellCastByName(Game::Lua::ToString(L, 1));

    // Spell not in the player's spellbook → match `CastSpellByName`'s
    // silent failure.
    Game::Lua::PushBool(L, dispatched && Spell::Placement::CommitAtCursor());
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kCastAtCursorArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier",
                   "A spell ID, which casts that exact rank, or a spell name, "
                   "which casts the highest rank you know unless it ends in \"(Rank N)\"."),
};
const Game::Doc::Field kCastAtCursorRets[] = {
    Game::Doc::Req("placed", "bool",
                   "True when the spell landed at the cursor; false when the cursor was "
                   "not on the ground, or the spell needs no ground target."),
};
const Game::Doc::Function kCastAtCursor{
    "Casts a ground-target spell at the cursor's world position, with no reticle click.",
    kCastAtCursorArgs, kCastAtCursorRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "CastAtCursor",
                                     &Script_C_Spell_CastAtCursor, &kCastAtCursor);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Tag macros that call `C_Spell.CastAtCursor(<spell>)` with the spellID,
// so action-bar UIs highlight the slot the same way they do for
// `/cast SpellName`.
const Spell::MacroPrimarySpell::PatternAutoRegister _patreg{
    "C_Spell.CastAtCursor(", &Spell::MacroPrimarySpell::ExtractSpellArg};

} // namespace

} // namespace Spell::AtCursor
