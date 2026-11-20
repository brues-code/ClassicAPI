// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Player↔unit proximity, modern's shape. Vanilla 1.12 ships neither
// function but has all the underlying machinery — the object's
// `GetPosition` virtual (vtable slot 5) `CheckInteractDistance` uses:
//
//   `UnitInRange(unit) → (inRange, checkedRange)` — 40-yard healing-
//     range check for the group, as the 3.80.2 client's
//     (Script_UnitInRange 0x140E39B80, core 0x141BB0D80) does it:
//       - Only group units are checked: you, a party or raid member, or
//         the pet of one, while you are in a group (its member list holds
//         you only then). Anything else, you and your pet when solo
//         included, is (false, false).
//       - A group unit is checked even when it is not in view: (false,
//         true) for a member out of view or on another map.
//       - It measures from the active mover, else the player; with
//         neither it is (false, false).
//     Reach-aware: matches the engine's own spell-range formula (see
//     below), so a 40-yard heal registers in-range against a target whose
//     *center* is slightly past 40 yards but whose body is within reach.
//     3.80.2 also wants two flag bits of the unit clear and no
//     restricted-targeting aura, and takes the range from an aura (effect
//     225) when one is up; 1.12 has no counterpart of these.
//   `UnitDistanceSquared(unit) → (distanceSquared, checkedPosition)` —
//     the raw squared world distance player→unit. Squared because
//     nearly every consumer compares against a threshold (`distSq <=
//     range*range`) or ranks by nearest, neither of which needs the
//     sqrt; that's why retail exposes only the squared form (added
//     5.0.4). Center-to-center — this one is NOT reach-adjusted (retail's
//     `UnitDistanceSquared` is also raw center distance); the reach
//     correction lives only in `UnitInRange`.
//
// Reach-awareness: the engine's spell-range helper `FUN_006e3480` compares
// center distance against `baseRange + reachCaster + reachTarget`, where
// each unit's reach is the bounding-radius float at `[m_objectFields +
// 0x1F0]` (the same size factor the interact-distance and loot-range
// checks add). So the effective in-range distance for a 40-yard heal is
// `40 + playerReach + targetReach`. On big targets that's easily 2–3
// yards past the raw 40, which is why a pure center-to-center 40-yard cap
// reported out-of-range for a target the client could actually heal.
//
// The active mover is `CGUnit_C::s_activeMover`: the unit the client moves,
// which is the player, or the unit it controls (Mind Control, Eyes of the
// Beast).

#include "unit/Range.h"

#include "Game.h"
#include "group/Roster.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/Position.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Unit::Range {

namespace {

constexpr float kRangeYards = 40.0f;

// Resolves `token` once and reads both its world position and bounding
// radius. Returns false when the position is unavailable (unresolved /
// absent token, or an object with no known position yet — e.g. a party
// member outside the client's sync range). This false is exactly the
// `checkedPosition = false` case retail flags.
bool ReadPosAndReach(const char *token, float pos[3], float *reach) {
    void *obj = Game::ResolveUnitToken(token);
    if (!Unit::Position::Read(obj, pos))
        return false;
    *reach = Unit::Position::BoundingRadius(obj);
    return true;
}

// Squared world distance from the player to `token`'s unit (raw,
// center-to-center; reach is applied by the caller when needed).
bool PlayerToUnitDistSq(const char *token, float *outSq) {
    float unitPos[3] = {};
    float playerPos[3] = {};
    float reach = 0.0f;
    if (!ReadPosAndReach(token, unitPos, &reach) ||
        !ReadPosAndReach("player", playerPos, &reach))
        return false;
    *outSq = Game::DistanceSq(unitPos, playerPos);
    return true;
}

int __fastcall Script_UnitInRange(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: local inRange, checkedRange = UnitInRange(unit)");
        return 0;
    }
    bool inRange = false;
    const bool checked = InRange(Unit::Identity::GuidForToken(Game::Lua::ToString(L, 1)), &inRange);
    Game::Lua::PushBool(L, inRange);
    Game::Lua::PushBool(L, checked);
    return 2;
}

// `UnitDistanceSquared("unit") → (distanceSquared, checkedPosition)`.
// Unlike UnitInRange there's no self-quirk: `UnitDistanceSquared("player")`
// is a legitimate `(0, true)`. On a position miss returns `(0, false)` —
// matching retail's "always a number" shape; consumers must branch on
// `checkedPosition`, since a real `0` (self, or exactly co-located units)
// is indistinguishable from the miss placeholder by value alone.
int __fastcall Script_UnitDistanceSquared(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitDistanceSquared(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    float distSq = 0.0f;
    if (!PlayerToUnitDistSq(token, &distSq)) {
        Game::Lua::PushNumber(L, 0.0);
        Game::Lua::PushBool(L, false);
        return 2;
    }

    Game::Lua::PushNumber(L, static_cast<double>(distSq));
    Game::Lua::PushBool(L, true);
    return 2;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kUnitArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kInRangeRets[] = {
    Game::Doc::Req("inRange", "bool", "True when the unit is in view and within reach of a 40-yard heal."),
    Game::Doc::Req("checkedRange", "bool",
                   "True for you, a group member or the pet of one while you are in a group."),
};
const Game::Doc::Function kUnitInRange{
    "Returns whether a group member is within 40 yards of you.", kUnitArgs, kInRangeRets,
    "UnitGlobals"};

const Game::Doc::Field kDistanceRets[] = {
    Game::Doc::Req("distance", "number",
                   "Squared distance in yards, center to center; 0 when checkedDistance is false."),
    Game::Doc::Req("checkedDistance", "bool", "False when the unit is not in view."),
};
const Game::Doc::Function kUnitDistanceSquared{
    "Returns the squared distance from you to a unit.", kUnitArgs, kDistanceRets,
    "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitInRange", &Script_UnitInRange, &kUnitInRange);
    Game::Lua::RegisterGlobalFunction("UnitDistanceSquared", &Script_UnitDistanceSquared,
                                      &kUnitDistanceSquared);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void *FromUnit() {
    if (void *mover = Object::ByGuid(Offsets::TYPEMASK_UNIT,
                                     Game::Read<uint64_t>(static_cast<uintptr_t>(Offsets::VAR_ACTIVE_MOVER_GUID))))
        return mover;
    return const_cast<uint8_t *>(Unit::Identity::PlayerObject());
}

bool InRange(uint64_t guid, bool *inRange) {
    *inRange = false;
    void *from = FromUnit();
    if (from == nullptr || !Group::Roster::IsGroupUnit(guid))
        return false;

    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    float unitPos[3] = {};
    float fromPos[3] = {};
    if (!Unit::Position::Read(unit, unitPos) || !Unit::Position::Read(from, fromPos))
        return true;

    // Effective range = base + both units' reach, mirroring the engine's
    // spell-range formula (FUN_006e3480). Without the reach terms a target
    // whose center sits just past 40 yards but whose body is within reach
    // reports out-of-range even though a 40-yard heal would land.
    const float effRange =
        kRangeYards + Unit::Position::BoundingRadius(from) + Unit::Position::BoundingRadius(unit);
    *inRange = Game::DistanceSq(unitPos, fromPos) <= effRange * effRange;
    return true;
}

} // namespace Unit::Range
