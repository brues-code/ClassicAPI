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

// UnitIsBehind backport.
//
// `UnitIsBehind("target") → isBehind` — returns true when the **player** is
// in the back 180deg hemisphere of the target unit, AND within melee range
// (5 yards). Designed for rogue backstab / feral shred.
//
// `UnitIsBehind("unit1", "unit2") → isBehind` — two-argument form; returns
// true when unit1 is in unit2's back hemisphere within melee range.
//
// The direction is computed with atan2 — numerically stable at all
// distances, unlike `acos(dot / |a||b|)` which explodes when position
// deltas approach zero. A tiny distance guard (< 0.01 yds) catches the
// degenerate atan2(0,0) case.
//
// Melee range cap: anything past 5 yards is out of melee reach, so no
// backstab is possible regardless of angle. For out-of-melee "behind"
// queries on friendly units — e.g. shaman not wanting to turn their back —
// use a manual UnitFacing+UnitPosition check without the range cap.
//
// Stationary-melee facing: in 1.12 the server does not push a facing update
// for a creature that stands still while it swings, so the client-side facing
// of a tank-and-spank mob can lag the direction it is actually attacking
// from. UnitXP_SP3's workaround is used here: for a creature in combat that
// is not moving and can see its current target, the direction to that target
// replaces its facing. A creature that cannot see its target is pathing
// somewhere, and the facing value stands.

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "unit/LineOfSight.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdint>

namespace Unit::Behind {

namespace {

constexpr float kMeleeRangeYards  = 5.0f;
constexpr float kMinDistForAngle  = 0.01f;
constexpr float kPI               = 3.14159265358979323846f;
constexpr float kHalfPI           = kPI * 0.5f;
constexpr float kTwoPI            = kPI * 2.0f;

// Vanilla MOVEFLAG_* bits that mean the unit is translating or turning
// (UnitXP_SP3's MOVEFLAG_MASK_MOVING_OR_TURN): forward/back/strafe, turn,
// pitch, jumping, falling far, spline elevation. A stationary meleeing
// creature has none of them set.
constexpr uint32_t kMoveFlagMask = 0x040060FF;

float NormalizeAngle(float a) {
    while (a >  kPI) { a -= kTwoPI; }
    while (a < -kPI) { a += kTwoPI; }
    return a;
}

// The facing to test against. Normally the unit's own facing; see the
// stationary-melee note at the top for the creature case.
float FacingForBackTest(void *unit) {
    const float facing = Unit::Position::Facing(unit);

    if (Game::Read<int>(unit, Offsets::OFF_CGOBJECT_TYPE_ID) !=
        Offsets::OBJECT_TYPE_UNIT)
        return facing;      // players turn for real; only creatures lag

    const uint8_t *fields =
        Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr)
        return facing;
    if ((Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
         Offsets::UNIT_FLAG_IN_COMBAT) == 0)
        return facing;

    const uint8_t *movement = Game::Read<const uint8_t *>(
        unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    if (movement == nullptr)
        return facing;
    if ((Game::Read<uint32_t>(movement, Offsets::OFF_MOVEMENT_FLAGS) &
         kMoveFlagMask) != 0)
        return facing;      // moving: the facing it walks with is current

    const uint64_t targetGuid =
        Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_TARGET);
    if (targetGuid == 0)
        return facing;
    void *target = Object::ByGuid(
        Offsets::TYPEMASK_UNIT | Offsets::TYPEMASK_PLAYER, targetGuid);
    if (target == nullptr || target == unit)
        return facing;
    if (Unit::LineOfSight::InSight(unit, target) <= 0)
        return facing;      // cannot see its target: it is pathing, keep facing

    float unitPos[3] = {};
    float targetPos[3] = {};
    if (!Unit::Position::Read(unit, unitPos) ||
        !Unit::Position::Read(target, targetPos))
        return facing;
    return std::atan2(targetPos[1] - unitPos[1], targetPos[0] - unitPos[0]);
}

bool IsBehind(void *unit1, void *unit2) {
    if (unit1 == nullptr || unit2 == nullptr)
        return false;
    if (unit1 == unit2)
        return false;

    float pos1[3] = {};
    float pos2[3] = {};
    if (!Unit::Position::Read(unit1, pos1) ||
        !Unit::Position::Read(unit2, pos2))
        return false;

    const float dx = pos1[0] - pos2[0];
    const float dy = pos1[1] - pos2[1];
    const float distSq = dx * dx + dy * dy;

    if (distSq > kMeleeRangeYards * kMeleeRangeYards)
        return false;
    if (distSq < kMinDistForAngle * kMinDistForAngle)
        return false;

    const float angleToUnit1 = std::atan2(dy, dx);
    const float facing2 = FacingForBackTest(unit2);

    const float diff = NormalizeAngle(angleToUnit1 - facing2);
    return std::fabs(diff) > kHalfPI;
}

int __fastcall Script_UnitIsBehind(void *L) {
    const char *token1 = nullptr;
    const char *token2 = nullptr;
    const int nargs = Game::Lua::GetTop(L);

    if (nargs == 1) {
        if (!Game::Lua::IsString(L, 1)) {
            Game::Lua::Error(L, "Usage: UnitIsBehind(\"unit\") "
                                 "or UnitIsBehind(\"unit1\", \"unit2\")");
            return 0;
        }
        token1 = "player";
        token2 = Game::Lua::ToString(L, 1);
    } else if (nargs == 2) {
        if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
            Game::Lua::Error(L, "Usage: UnitIsBehind(\"unit\") "
                                 "or UnitIsBehind(\"unit1\", \"unit2\")");
            return 0;
        }
        token1 = Game::Lua::ToString(L, 1);
        token2 = Game::Lua::ToString(L, 2);
    } else {
        Game::Lua::Error(L, "Usage: UnitIsBehind(\"unit\") "
                             "or UnitIsBehind(\"unit1\", \"unit2\")");
        return 0;
    }

    void *unit1 = Unit::Position::ResolveToken(token1);
    void *unit2 = Unit::Position::ResolveToken(token2);

    Game::Lua::PushBool(L, IsBehind(unit1, unit2));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitIsBehind", &Script_UnitIsBehind);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Behind