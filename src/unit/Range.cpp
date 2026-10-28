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
//     range check. Reach-aware: matches the engine's own spell-range
//     formula (see below), so a 40-yard heal registers in-range against
//     a target whose *center* is slightly past 40 yards but whose body
//     is within reach.
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
// UnitInRange measures from the active mover, as 4.3.4's does
// (`CGUnit_C::s_activeMover`): the unit the client moves, which is the
// player, or the unit it controls (Mind Control, Eyes of the Beast). No
// mover gives no answer, as in 4.3.4.

#include "Game.h"
#include "object/Resolve.h"
#include "unit/Position.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Unit::Range {

namespace {

constexpr float kRangeYards = 40.0f;

// Reads a unit's bounding radius (the engine's per-unit range "size
// factor") from `[m_objectFields + 0x1F0]`. Returns 0 for a null object
// or a unit whose descriptor isn't populated yet — a safe fallback that
// degrades to the plain base-range check. Same field the loot/interact
// range checks (loot/Nearby.cpp) read.
float BoundingRadius(void *obj) {
    if (obj == nullptr)
        return 0.0f;
    auto *fields = *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(obj) + Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr)
        return 0.0f;
    return *reinterpret_cast<const float *>(
        fields + Offsets::OFF_UNIT_FIELD_BOUNDING_RADIUS);
}

// Resolves `token` once and reads both its world position and bounding
// radius. Returns false when the position is unavailable (unresolved /
// absent token, or an object with no known position yet — e.g. a party
// member outside the client's sync range). This false is exactly the
// `checkedPosition = false` case retail flags.
bool ReadPosAndReach(const char *token, float pos[3], float *reach) {
    void *obj = Unit::Position::ResolveToken(token);
    if (!Unit::Position::Read(obj, pos))
        return false;
    *reach = BoundingRadius(obj);
    return true;
}

// The active mover's world position and reach (VAR_ACTIVE_MOVER_GUID).
bool ReadMoverPosAndReach(float pos[3], float *reach) {
    void *mover = Object::ByGuid(
        Offsets::TYPEMASK_UNIT,
        Game::Read<uint64_t>(static_cast<uintptr_t>(Offsets::VAR_ACTIVE_MOVER_GUID)));
    if (!Unit::Position::Read(mover, pos))
        return false;
    *reach = BoundingRadius(mover);
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
    const float dx = unitPos[0] - playerPos[0];
    const float dy = unitPos[1] - playerPos[1];
    const float dz = unitPos[2] - playerPos[2];
    *outSq = dx * dx + dy * dy + dz * dz;
    return true;
}

int __fastcall Script_UnitInRange(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitInRange(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    // Documented Blizzard quirk: `UnitInRange("player")` returns
    // false. The function is meant for healing-frame "is *another*
    // unit in range" checks; querying yourself is meaningless and
    // the API returns nil/false to flag that. We match the modern
    // behavior — no position read, no range check.
    if (token != nullptr && std::strcmp(token, "player") == 0) {
        Game::Lua::PushBool(L, false);
        Game::Lua::PushBool(L, false);
        return 2;
    }

    float unitPos[3] = {};
    float moverPos[3] = {};
    float unitReach = 0.0f;
    float moverReach = 0.0f;
    if (!ReadPosAndReach(token, unitPos, &unitReach) ||
        !ReadMoverPosAndReach(moverPos, &moverReach)) {
        Game::Lua::PushBool(L, false);
        Game::Lua::PushBool(L, false);
        return 2;
    }

    const float dx = unitPos[0] - moverPos[0];
    const float dy = unitPos[1] - moverPos[1];
    const float dz = unitPos[2] - moverPos[2];
    const float distSq = dx * dx + dy * dy + dz * dz;

    // Effective range = base + both units' reach, mirroring the engine's
    // spell-range formula (FUN_006e3480). Without the reach terms a target
    // whose center sits just past 40 yards but whose body is within reach
    // reports out-of-range even though a 40-yard heal would land.
    const float effRange = kRangeYards + moverReach + unitReach;
    Game::Lua::PushBool(L, distSq <= effRange * effRange);
    Game::Lua::PushBool(L, true);
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

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitInRange", &Script_UnitInRange);
    Game::Lua::RegisterGlobalFunction("UnitDistanceSquared",
                                      &Script_UnitDistanceSquared);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Range
