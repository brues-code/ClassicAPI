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

// `C_Spell.GetSpellEffectInfo(spellID)` -> { fx1, fx2, fx3 }
//
// One table per spell effect, each holding that effect's raw Spell.dbc
// fields:
//
//   effect          SPELL_EFFECT_* code (0 = the slot is unused)
//   auraName        EffectApplyAuraName (0 = the effect applies no aura)
//   basePoints      EffectBasePoints
//   baseDice        EffectBaseDice
//   dieSides        EffectDieSides
//   pointsPerLevel  EffectRealPointsPerLevel (fractional)
//   dicePerLevel    EffectDicePerLevel (fractional)
//   miscValue       EffectMiscValue — the effect's parameter, e.g. which
//                   stat a stat-modifying aura applies to
//
// Deliberately RAW. The obvious alternative — return one computed
// magnitude per effect — would be wrong often enough to mislead, because
// the engine's figure also folds in a die roll (unreproducible on this
// side), combo points against the combo target, the caster's spell mods,
// and a level rescale for spells flagged for it. A number that carries
// none of those while looking authoritative is worse than the inputs.
//
// The fixed part is `basePoints + baseDice`, which is the whole answer
// whenever `dieSides <= 1` and `pointsPerLevel` is 0 — the common case
// for aura magnitudes. For the general form:
//
//   local sl, _, ml = C_Spell.GetSpellLevelInfo(spellID)
//   local level = math.max(sl, math.min(UnitLevel("player"), ml > 0 and ml or 99)) - sl
//   local points = fx.basePoints + level * fx.pointsPerLevel
//   local random = fx.dieSides   + level * fx.dicePerLevel
//   local value  = points + (random <= 1 and fx.baseDice or nil) -- else a roll
//
// A `dieSides` above 1 means the magnitude is a server-side roll; treat
// that effect as having no single value rather than guessing one.
//
// Complements `C_Spell.GetSpellEffectMechanics`, which returns the
// parallel EffectMechanic[3] array on its own. Reads Spell.dbc directly,
// so it covers every spell the client knows — not just the player's
// spellbook — with no caching or network round-trip.

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::EffectInfo {

namespace {

int __fastcall Script_GetSpellEffectInfo(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0; // nil for invalid / out-of-range spell IDs

    auto *effect = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT);
    auto *auraName = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
    auto *basePoints = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_BASE_POINTS);
    auto *baseDice = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_BASE_DICE);
    auto *dieSides = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_DIE_SIDES);
    auto *dicePerLevel = reinterpret_cast<const float *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_DICE_PER_LEVEL);
    auto *pointsPerLevel = reinterpret_cast<const float *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_REAL_POINTS_PER_LEVEL);
    auto *miscValue = reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE);

    // Fixed 1-based array of all three effects, matching the raw [3]
    // layout so callers can index by effect slot. An unused slot is still
    // present, with every field 0.
    Game::Lua::NewTable(L); // the outer array
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1)); // outer key
        Game::Lua::NewTable(L);                               // the effect
        // Each SetFieldNumber pushes key + value and assigns into the
        // table below them, so they all land in the effect table on top.
        Game::Lua::SetFieldNumber(L, "effect", static_cast<double>(effect[i]));
        Game::Lua::SetFieldNumber(L, "auraName",
                                  static_cast<double>(auraName[i]));
        Game::Lua::SetFieldNumber(L, "basePoints",
                                  static_cast<double>(basePoints[i]));
        Game::Lua::SetFieldNumber(L, "baseDice",
                                  static_cast<double>(baseDice[i]));
        Game::Lua::SetFieldNumber(L, "dieSides",
                                  static_cast<double>(dieSides[i]));
        Game::Lua::SetFieldNumber(L, "pointsPerLevel",
                                  static_cast<double>(pointsPerLevel[i]));
        Game::Lua::SetFieldNumber(L, "dicePerLevel",
                                  static_cast<double>(dicePerLevel[i]));
        Game::Lua::SetFieldNumber(L, "miscValue",
                                  static_cast<double>(miscValue[i]));
        Game::Lua::SetTable(L, -3); // outer[i + 1] = effect table
    }
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellEffectInfo",
                                     &Script_GetSpellEffectInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::EffectInfo
