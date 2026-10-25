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

#include "Range.h"

#include "Game.h"
#include "Offsets.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>

namespace Spell::Range {

namespace {

// FUN_SPELL_RANGE_CHECK: char __fastcall(caster, target, spellID, outFlag).
using SpellRangeCheck_t = char(__fastcall *)(void *caster, void *target,
                                             int spellID, char *outFlag);

// FUN_GET_MIN_MAX_RANGE: void __fastcall(caster, spellID, min, max, target).
using GetMinMaxRange_t = void(__fastcall *)(void *caster, uint32_t spellID,
                                            float *minRange, float *maxRange,
                                            void *target);

// The engine's own has-range notion, mirroring `ActionHasRange`
// (`0x4E75D0`): either bound at or above the near-zero epsilon counts as
// ranged. The negated `<` (rather than `>=`) reproduces the engine's
// `fcomp` + `test ah,5; jp` exactly — a NaN bound counts as ranged,
// matching vanilla. Unlike the raw `SpellRange.dbc` read this replaces,
// the engine call also covers `Attributes & 0x404` spells (flat 100-yard
// max with no range row), melee rows whose max folds combat reach, and
// the stationary / ranged-item / spell-mod bonuses.
bool EngineHasRange(void *caster, int spellID, void *target) {
    float minRange = 0.0f;
    float maxRange = 0.0f;
    auto getMinMax = reinterpret_cast<GetMinMaxRange_t>(
        Offsets::FUN_GET_MIN_MAX_RANGE);
    getMinMax(caster, static_cast<uint32_t>(spellID), &minRange, &maxRange,
              target);
    const float epsilon = Game::Read<float>(
        static_cast<uintptr_t>(Offsets::VAR_HAS_RANGE_EPSILON));
    return !(std::fabs(minRange) < epsilon) ||
           !(std::fabs(maxRange) < epsilon);
}

} // namespace

bool SpellIDHasRange(int spellID) {
    if (spellID <= 0)
        return false;
    // Null target: the engine falls back to the caster's current-target
    // GUID (`[caster+0xC48]`), else the caster itself, for the reach fold.
    // A null caster zeroes both bounds, which reads as rangeless.
    return EngineHasRange(Unit::Position::ResolveToken("player"), spellID,
                          nullptr);
}

int PlayerVsUnit(int spellID, const char *unitToken) {
    // No answer when the range check can't apply: unknown spell or no
    // unit token given.
    if (spellID <= 0 || unitToken == nullptr)
        return -1;

    void *caster = Unit::Position::ResolveToken("player");
    void *target = Unit::Position::ResolveToken(unitToken);
    if (caster == nullptr || target == nullptr)
        return -1;
    // Rangeless spell (self buff, etc.) has no meaningful "in range" answer.
    if (!EngineHasRange(caster, spellID, target))
        return -1;

    // Guard positions before handing the objects to the engine core:
    // FUN_SPELL_RANGE_CHECK dereferences each GetPosition result without a
    // null check, and a resolved-but-position-less object would otherwise
    // crash. A failed read → no answer.
    float casterPos[3];
    float targetPos[3];
    if (!Unit::Position::Read(caster, casterPos) ||
        !Unit::Position::Read(target, targetPos))
        return -1;

    char minMaxFlag = 0;
    auto check =
        reinterpret_cast<SpellRangeCheck_t>(Offsets::FUN_SPELL_RANGE_CHECK);
    return check(caster, target, spellID, &minMaxFlag) != 0 ? 1 : 0;
}

} // namespace Spell::Range
