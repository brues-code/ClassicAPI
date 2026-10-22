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

// `C_Spell.CastAtUnit(spellIDOrName, unit)` — cast a spell at the given
// unit, whatever its target type. One primitive covering both:
//
//   - **Ground-target spells** (Blizzard, Flamestrike, Rain of Fire…) are
//     placed at the unit's feet — the reticle is committed at the unit's
//     world position, no manual click.
//   - **Unit-target / normal spells** (Frostbolt, Healing Touch, a buff…)
//     fire directly on the unit — its GUID is fed to the engine's cast
//     dispatcher as the implicit target, so no current-target juggling and
//     no "select target" reticle.
//
// Mechanism: resolve the token once to the unit object, read its position
// and GUID, then dispatch the cast WITH that GUID
// (`Spell::Placement::DispatchSpellCast(spell, guid)`). The engine's
// dispatcher substitutes the GUID exactly where it would the current
// selection, so a unit-target spell fires immediately at that unit. A
// ground spell ignores the GUID and enters placement mode instead — so we
// branch on `Spell::Placement::IsActive()` (`FinishAtUnit`):
//
//   - No placement after dispatch → the cast already went out on the unit
//     (unit-target / normal). Success.
//   - Ground placement active     → commit at the unit's feet. Success.
//   - Other placement still active → the engine didn't accept the unit as a
//     target (e.g. wrong faction / out of range); it is cancelled. Failure.
//
// When the call is going to place a ground spell, any placement already up
// is cancelled before the dispatch, so the one committed is this call's own.
// With `placeGroundSpell` false nothing is cancelled first: the reticle is
// the player's to click, and pressing the spell again while it is up stops
// targeting, as it does by hand.
//
// Returns `true` when the spell was cast at the unit; `false` when the
// spell isn't in the spellbook, the unit has no resolvable position, or the
// unit wasn't a valid target. A genuinely unrecognized token string raises
// the engine's standard "Unknown unit" error inside the resolver, same as
// `UnitHealth("garbage")`.

#include "Game.h"
#include "spell/AtUnit.h"
#include "spell/MacroPrimarySpell.h"
#include "spell/Placement.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cstdint>

namespace Spell::AtUnit {

namespace {

// The shared cast-at-unit core, used by both the Lua entry and the C++ API.
// Resolves the token once → object, reads position + GUID, dispatches the cast
// WITH that GUID, then handles the ground-spell placement branch. `byNumber`
// selects spellID vs spellName. Fail-fast so a bad/absent unit doesn't leave
// the engine mid-cast.
bool CastCore(const char *token, bool byNumber, int spellID, const char *spellName,
              bool allowGroundPlacement) {
    void *unitObj = Unit::Position::ResolveToken(token);
    float pos[3];
    if (unitObj == nullptr || !Unit::Position::Read(unitObj, pos))
        return false;
    const uint64_t targetGuid = Unit::Identity::GuidForObject(unitObj);

    if (allowGroundPlacement)
        Spell::Placement::Stop();
    const bool dispatched =
        byNumber ? Spell::Placement::DispatchSpellCast(spellID, targetGuid)
                 : Spell::Placement::DispatchSpellCastByName(spellName, targetGuid);
    if (!dispatched)
        return false;
    return Spell::Placement::FinishAtUnit(pos, allowGroundPlacement);
}

int __fastcall Script_C_Spell_CastAtUnit(void *L) {
    // arg1: numeric spellID (exact rank) or spell name ("(Rank N)"-aware).
    // arg2: unit token.
    // arg3: place a ground-target spell at the unit — defaults to true, so
    //       omitting it keeps the whole primitive. `false` casts on the unit
    //       but leaves a ground spell's reticle for the player.
    const bool byNumber = Game::Lua::IsNumber(L, 1);
    if ((!byNumber && !Game::Lua::IsString(L, 1)) ||
        !Game::Lua::IsString(L, 2)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const char *token = Game::Lua::ToString(L, 2);
    // Omitted or nil means the default (place it); only an explicit false
    // holds the reticle. `lua_type` reports NONE, not nil, past the top of
    // the stack, so test the top rather than the type alone.
    const bool allowGround = Game::Lua::GetTop(L) < 3 ||
                             Game::Lua::Type(L, 3) == Game::Lua::TYPE_NIL ||
                             Game::Lua::ToBoolean(L, 3) != 0;
    const bool ok =
        byNumber
            ? CastCore(token, true, static_cast<int>(Game::Lua::ToNumber(L, 1)),
                       nullptr, allowGround)
            : CastCore(token, false, 0, Game::Lua::ToString(L, 1), allowGround);
    Game::Lua::PushBool(L, ok);
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kCastAtUnitArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier",
                   "A spell ID, which casts that exact rank, or a spell name, "
                   "which casts the highest rank you know unless it ends in \"(Rank N)\"."),
    Game::Doc::Req("unit", "UnitToken", "The unit to cast the spell at."),
    Game::Doc::Opt("placeGroundSpell", "bool", "true",
                   "False leaves a ground-target spell's reticle for you to click."),
};
const Game::Doc::Field kCastAtUnitRets[] = {
    Game::Doc::Req("cast", "bool",
                   "True when the spell went out at the unit; false when you do not know "
                   "the spell, or the unit is not a target the spell accepts."),
};
const Game::Doc::Function kCastAtUnit{
    "Casts a spell at a unit, dropping a ground-target spell at that unit's feet.",
    kCastAtUnitArgs, kCastAtUnitRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "CastAtUnit",
                                     &Script_C_Spell_CastAtUnit, &kCastAtUnit);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Tag macros calling `C_Spell.CastAtUnit(<spell>, ...)` with the spellID
// so action-bar UIs highlight the slot.
const Spell::MacroPrimarySpell::PatternAutoRegister _patreg{
    "C_Spell.CastAtUnit(", &Spell::MacroPrimarySpell::ExtractSpellArg};

} // namespace

// ---- C++ API (see AtUnit.h) — same core as the Lua entry, no round-trip -----

bool CastByName(const char *spellName, const char *unitToken, bool placeGroundSpell) {
    if (spellName == nullptr || unitToken == nullptr)
        return false;
    return CastCore(unitToken, false, 0, spellName, placeGroundSpell);
}

bool CastByID(int spellID, const char *unitToken) {
    if (unitToken == nullptr)
        return false;
    return CastCore(unitToken, true, spellID, nullptr, /*allowGroundPlacement*/ true);
}

} // namespace Spell::AtUnit
