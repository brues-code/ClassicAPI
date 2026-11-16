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

#pragma once

// A stretch of a combat log, loaded for replay: each unit in it, where it was
// and what it looked like at any time. Times are milliseconds from the
// stretch's start (negative before it). The units come from the movement file
// (combatlog/Movement.cpp documents its lines) and the combat log's names.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Replay {

// UNIT_INFO's look.
struct Look {
    uint32_t display = 0;
    uint32_t nativeDisplay = 0;
    uint32_t mountDisplay = 0;
    float scale = 1.0f;
    uint32_t bytes1 = 0; // UNIT_FIELD_BYTES_1: stand state, form, stealth
    uint32_t bytes2 = 0; // UNIT_FIELD_BYTES_2: sheath state
};

// PLAYER_INFO: a player's own body and what it wears, as the movement file
// writes them. Of its PLAYER_FLAGS only the two its model reads.
struct Appearance {
    uint32_t race = 0, classId = 0, sex = 0;
    uint32_t skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
    uint32_t hideHelm = 0, hideCloak = 0;
    static constexpr int kSlots = 19;
    uint32_t items[kSlots] = {}; // inventory slot n at [n - 1]
    uint32_t enchants[kSlots] = {};
};

// The speeds the client moves a unit at (UNIT_INFO, then UNIT_SPEED).
struct Speeds {
    float walk = 0, run = 0, runBack = 0, swim = 0, swimBack = 0;
};

// A MONSTER_MOVE path: the unit walks its points at one speed over the
// duration, the way the client moves it.
struct Spline {
    int32_t durationMs = 0;
    int faceType = 0; // the server's: 2 a point, 3 a unit, 4 an angle
    float facePoint[3] = {};
    float faceAngle = 0.0f;
    int32_t faceUnit = -1; // index of the unit faced, for type 3
    std::vector<float> points; // x, y, z of each point, the start first
    std::vector<float> lengths; // the distance along the path to each point
};

enum class KeyKind : uint8_t {
    Sample, // a player's move: the unit goes on to a next Sample in a line
    Snap,   // a place the unit is put at (into view, a teleport, a stop)
    Spline, // the start of a path (Unit::splines)
};

struct Key {
    int32_t t = 0;
    KeyKind kind = KeyKind::Sample;
    float x = 0, y = 0, z = 0, facing = 0;
    int32_t spline = -1;
    // A UNIT_MOVE's status: its clock, movement flags and fall time. The
    // loader times the unit's moves from them. In the air (flag 0x2000) the
    // fall began `fallTime` ms before at `fallSpeed` (a jump's is negative).
    bool clocked = false;
    uint32_t clock = 0;
    uint32_t flags = 0;
    uint32_t fallTime = 0;
    float fallSpeed = 0;
};

// While the unit was in view: from `from` until before `to`.
struct Span {
    int32_t from = 0;
    int32_t to = INT32_MAX;
};

// An aura on a unit, from its SPELL_AURA_APPLIED until before its
// SPELL_AURA_REMOVED (or the stretch's end).
struct AuraSpan {
    uint32_t spellID = 0;
    bool debuff = false;
    int32_t caster = -1; // the applier's unit index, -1 for none
    int32_t from = 0;
    int32_t to = INT32_MAX;
};

struct Unit {
    std::string guid; // as the logs write it
    std::string name; // from the combat log, empty when it is not in it
    bool player = false;
    uint32_t creatureID = 0;
    std::vector<Key> keys;
    std::vector<Spline> splines;
    std::vector<Span> spans; // empty when the movement file has no UNIT_INFO for it
    std::vector<std::pair<int32_t, Look>> looks;
    std::vector<std::pair<int32_t, Appearance>> appearances; // a player's
    std::vector<AuraSpan> auras;
    // While it was dead: from its UNIT_DIED until it next cast or attacked,
    // was resurrected, or left view.
    std::vector<Span> deaths;
    std::vector<std::pair<int32_t, Speeds>> speeds;
};

// A combat log line of the stretch, as the replay plays it.
struct CombatEvent {
    int32_t t = 0;
    std::string type; // the subevent as the log names it: SPELL_CAST_START, SWING_DAMAGE, ...
    int32_t source = -1; // unit indexes, -1 for none or not a unit
    int32_t dest = -1;
    uint32_t spellID = 0; // a SPELL_, RANGE_ or DAMAGE_ event's, else 0
    bool debuff = false; // an aura event's type
    // A SPELL_CAST_START: when the caster's SPELL_CAST_SUCCESS or
    // SPELL_CAST_FAILED of that spell came, or -1; and whether it failed.
    int32_t castEnd = -1;
    bool castFailed = false;
    bool endsCast = false; // a SPELL_CAST_SUCCESS or FAILED that ends a SPELL_CAST_START
};

struct Recording {
    std::string log;
    int64_t logStartMs = 0; // the log's first stamp
    int64_t startMs = 0; // the stretch, as stamps
    int64_t endMs = 0;
    int map = -1;
    bool movement = false; // a movement file was read
    std::vector<Unit> units;
    std::vector<CombatEvent> events; // in time order
};

// Where a unit is, which way it faces, and how fast it moves, in yards a
// second; and whether it is in the air, and got there by a jump.
struct Place {
    float x, y, z, facing, speed;
    bool falling = false;
    bool jumped = false;
};

// The unit's place at `t`. False while it is not in view, or not yet placed.
bool PlaceAt(const Recording &recording, const Unit &unit, int32_t t, Place &place);

// The unit's look at `t`, null before its first.
const Look *LookAt(const Unit &unit, int32_t t);

// Whether the unit is dead at `t`.
bool DeadAt(const Unit &unit, int32_t t);

// Loads the stretch [startMs, endMs] (stamps) of a log (a Catalog name). Null
// when the combat log cannot be read. Worker thread safe.
std::unique_ptr<Recording> Load(const std::string &log, int64_t startMs, int64_t endMs);

} // namespace Replay
