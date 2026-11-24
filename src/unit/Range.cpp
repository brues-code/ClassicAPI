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
//   `UnitDistanceSquared(unit) → (distance, checkedDistance)` — the
//     3.80.2 client's (binder 0x140E35D80, core 0x141BB0FD0; see
//     DistanceSquared in Range.h): x and y only, squared, rounded to a whole
//     number, center to center (no reach), from FromUnit.
//       - Its gate (0x141698520) passes you and group members on open-world
//         maps; for those, checkedDistance is true even with no position
//         (the distance stays 0). Anything else is (0, false).
//       - Here the gate is you and every group unit, on every map, and any
//         other unit is measured while it is in view: (distance, true), else
//         (0, false).
//       - The unit argument is a generated reader's (0x140B296B0): missing
//         or not a string is the usage error (lua/Arg's String), and a
//         string the resolver does not know is no unit.
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
#include "dbc/Lookup.h"
#include "group/MemberStats.h"
#include "group/Roster.h"
#include "lua/Arg.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/Position.h"
#include "Offsets.h"

#include <cmath>
#include <cstdint>

namespace Unit::Range {

namespace {

constexpr float kRangeYards = 40.0f;

constexpr const char *kDistanceUsage = "Usage: local distance, checkedDistance = UnitDistanceSquared(unit)";

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

int __fastcall Script_UnitDistanceSquared(void *L) {
    Game::Lua::Arg::Text token;
    Game::Lua::Arg::String(L, 1, kDistanceUsage, token);
    const uint64_t guid = Unit::Identity::GuidForToken(token.data);

    long long squared = 0;
    const Distance found = DistanceSquared(guid, &squared);
    const bool checked =
        found == Distance::Measured ||
        (found != Distance::NoFromUnit &&
         (guid == Unit::Identity::PlayerGuid() || Group::Roster::IsGroupUnit(guid)));
    Game::Lua::PushNumber(L, static_cast<double>(squared));
    Game::Lua::PushBool(L, checked);
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
                   "Squared distance in yards, center to center, on the ground (no height), as a whole number; "
                   "0 when it is not known."),
    Game::Doc::Req("checkedDistance", "bool",
                   "True for you and your group units, also with no position. For other units, true when "
                   "the unit is in view."),
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

Distance DistanceSquared(uint64_t guid, long long *squared) {
    *squared = 0;
    float from[3] = {};
    if (!Unit::Position::Read(FromUnit(), from))
        return Distance::NoFromUnit;

    float pos[3] = {};
    if (!Unit::Position::Read(Object::ByGuid(Offsets::TYPEMASK_UNIT, guid), pos)) {
        int map = -1;
        if (!Group::MemberStats::Position(guid, &pos[0], &pos[1], &map))
            return Distance::NoPosition;
        const int playerMap = DBC::AreaMap(Game::Read<uint32_t>(Offsets::VAR_PLAYER_AREA_ID) & 0xFFFF);
        if (map != -1 && playerMap != -1 && map != playerMap)
            return Distance::OtherMap;
    }
    const float dx = pos[0] - from[0];
    const float dy = pos[1] - from[1];
    *squared = std::llround(dx * dx + dy * dy);
    return Distance::Measured;
}

} // namespace Unit::Range
