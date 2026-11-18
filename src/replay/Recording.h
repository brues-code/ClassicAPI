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
    // A creature's held items, main hand, off hand, ranged: each
    // UNIT_VIRTUAL_ITEM_SLOT_DISPLAY (an ItemDisplayInfo id) and its two
    // UNIT_VIRTUAL_ITEM_INFO dwords, which are the unit's held lane: class,
    // subclass, material, inventory type, then the sheathe type.
    static constexpr int kHands = 3;
    uint32_t heldDisplay[kHands] = {};
    uint32_t heldInfo[kHands * 2] = {};
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
    // Its guild's tabard emblem (style, colour, border, border colour,
    // background); -1 unknown.
    int32_t emblem[5] = {-1, -1, -1, -1, -1};
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

// A unit's health as an advanced block gives it: a player's as a percent of
// 100, a creature's raw.
struct Health {
    int32_t current = 0;
    int32_t max = 0;
};

// An aura on a unit, from its SPELL_AURA_APPLIED, or the UNIT_AURAS line
// that first has it, until before its SPELL_AURA_REMOVED, the UNIT_AURAS
// line that no longer has it, or the unit leaving view.
struct AuraSpan {
    uint32_t spellID = 0;
    bool debuff = false;
    int32_t caster = -1; // the applier's unit index, -1 for none
    int32_t from = 0;
    int32_t to = INT32_MAX;
    // Whether `to` is the aura's own end (its SPELL_AURA_REMOVED, or a
    // UNIT_AURAS line without it), not the unit leaving view.
    bool ended = false;
    // When its duration last began, as the logs saw it: its application,
    // a refresh, or a stack added. INT32_MIN when they did not (it was on
    // the unit when the unit came into view).
    int32_t renewed = INT32_MIN;
    // Its stacks, each from the time it changed: from UNIT_AURAS lines and
    // SPELL_AURA_APPLIED_DOSE / _REMOVED_DOSE. Empty when neither gives them.
    std::vector<std::pair<int32_t, uint32_t>> stacks;
};

// UNIT_CAST: the spell a unit casts with its cast time, and the spell it
// channels; 0 for none.
struct CastState {
    int32_t t = 0;
    uint32_t spell = 0;
    uint32_t castMs = 0;
    uint32_t channel = 0;
};

// A cast bar: a cast from `start`, due to end at `end`, under way until
// `stop`. A cast that went off stops at its end; an interrupted one before
// it. A channel runs from `start` until `stop`; its `end` is -1, as the logs
// do not give its length.
struct Cast {
    uint32_t spellID = 0;
    int32_t start = 0;
    int32_t end = -1;
    int32_t stop = INT32_MAX;
    bool channel = false;
    bool interrupted = false;
    int32_t interrupter = -1; // the unit whose SPELL_INTERRUPT stopped it
};

// A player's guild from its PLAYER_INFO lines, as GetGuildInfo(unit) gave it
// on the recorder's client: the guild's and the rank's names, empty until
// that client had the guild, and the rank (0 the guild master).
struct Guild {
    std::string name;
    std::string rankName;
    int32_t rank = -1; // -1 with no guild
    bool operator==(const Guild &o) const { return rank == o.rank && name == o.name && rankName == o.rankName; }
};

// UNIT_STATE: whom the unit targets and who owns it (unit indexes, -1 for
// none), UNIT_FIELD_FLAGS, its level, its FactionTemplate id, its dynamic
// flags, and from the recording player's view its reaction toward that
// player (0 .. 7) and its selection type.
struct UnitState {
    int32_t target = -1;
    int32_t owner = -1;
    int32_t attackTarget = -1; // the unit it swings at
    uint32_t flags = 0;
    uint32_t level = 0;
    uint32_t faction = 0;
    uint32_t dynamicFlags = 0;
    uint32_t reaction = 3;
    uint32_t selection = 2;
    std::string ownerTitle; // the tooltip's owner line, empty for none
};

// UNIT_POWER: health, and the power of the unit's own type in stored units.
struct Vitals {
    uint32_t health = 0, maxHealth = 0;
    uint32_t powerType = 0, power = 0, maxPower = 0;
};

struct Unit {
    std::string guid; // as the logs write it
    std::string name; // from the combat log, empty when it is not in it
    std::string cachedName; // the recorder's own name for it (UNIT_NAME)
    bool player = false;
    uint32_t creatureID = 0;
    std::vector<Key> keys;
    std::vector<Spline> splines;
    std::vector<Span> spans; // empty when the movement file has no UNIT_INFO for it
    std::vector<std::pair<int32_t, Look>> looks;
    std::vector<std::pair<int32_t, Appearance>> appearances; // a player's
    std::vector<std::pair<int32_t, Guild>> guilds; // a player's, each from the time it changed
    // A player's name with its PvP rank title as the recorder's client worded
    // it (PLAYER_INFO's pvpName), each from the time it changed.
    std::vector<std::pair<int32_t, std::string>> pvpNames;
    std::vector<AuraSpan> auras;
    // Its health at each advanced block that describes it, and 0 at its
    // UNIT_DIED.
    std::vector<std::pair<int32_t, Health>> health;
    // While it was dead: from its UNIT_DIED until it next cast or attacked,
    // was resurrected, or left view.
    std::vector<Span> deaths;
    std::vector<std::pair<int32_t, Speeds>> speeds;
    std::vector<CastState> castStates;
    std::vector<Cast> casts; // in start order
    // Its raid target mark (1 star to 8 skull, 0 none) from the raid flags of
    // each combat line that names it, each time it changes. The log writes
    // marks only while its recorder was in a group.
    std::vector<std::pair<int32_t, uint8_t>> marks;
    // From the movement file, each from the time it changed.
    std::vector<std::pair<int32_t, UnitState>> states;
    std::vector<std::pair<int32_t, Vitals>> vitals;
};

// A game object's GAMEOBJECT_INFO fields.
struct ObjectLook {
    int32_t owner = -1; // its creator's unit index, -1 for none
    uint32_t display = 0;
    uint32_t type = 0;  // GAMEOBJECT_TYPE_ID
    uint32_t state = 0; // GAMEOBJECT_STATE
    uint32_t flags = 0;
    float x = 0, y = 0, z = 0, facing = 0;
    float rotation[4] = {0, 0, 0, 1}; // the quaternion x, y, z, w
    float scale = 1;
};

struct GameObject {
    std::string guid; // as the logs write it
    std::string name; // from its record, empty when the recorder had none
    uint32_t objectID = 0;
    std::vector<Span> spans; // while in view
    std::vector<std::pair<int32_t, ObjectLook>> looks; // each from the time it changed
};

// The game clock from a GAME_TIME line: at `t`, the time of day (0 midnight,
// 0.5 noon), moving on at `scale` game minutes a real second.
struct GameClock {
    int32_t t;
    float timeOfDay;
    float scale;
};

// A spell's ground area (DYNAMICOBJECT_INFO), for one time in view.
struct GroundArea {
    std::string guid;
    int32_t caster = -1; // unit index
    uint32_t spellID = 0;
    uint32_t bytes = 0; // DYNAMICOBJECT_BYTES
    float radius = 0;
    float x = 0, y = 0, z = 0, facing = 0;
    Span span;
};

// A combat log line of the stretch, as the replay plays it.
struct CombatEvent {
    int32_t t = 0;
    std::string type; // the subevent as the log names it: SPELL_CAST_START, SWING_DAMAGE, ...
    int32_t source = -1; // unit indexes, -1 for none or not a unit
    int32_t dest = -1;
    // The line's source and dest flags (COMBATLOG_OBJECT_*): affiliation,
    // reaction, control and type as the recorder's client saw them. 0 on an
    // EMOTE, which has none.
    uint32_t sourceFlags = 0;
    uint32_t destFlags = 0;
    uint32_t spellID = 0; // a SPELL_, RANGE_ or DAMAGE_ event's, else 0
    std::string spellName; // its name as the log writes it
    bool debuff = false; // an aura event's type
    // A SPELL_CAST_START: when the caster's SPELL_CAST_SUCCESS or
    // SPELL_CAST_FAILED of that spell came, or -1; and whether it failed.
    // A start can also end at a SPELL_INTERRUPT of the spell on the caster
    // (`interrupter` its source) or at the caster's UNIT_DIED; both fail it.
    int32_t castEnd = -1;
    bool castFailed = false;
    int32_t interrupter = -1;
    // A SPELL_CAST_START: the destination of the SPELL_CAST_SUCCESS that ended
    // it, where its missile flies; -1 for none.
    int32_t castTarget = -1;
    bool endsCast = false; // a SPELL_CAST_SUCCESS or FAILED that ends a SPELL_CAST_START
    uint32_t extraSpellID = 0; // a SPELL_INTERRUPT's interrupted or a SPELL_DISPEL's removed spell
    std::string extraSpellName;
    // A damage or heal event's amount (`hasAmount`) and whether it was
    // critical; a _MISSED event's miss type (MISS, DODGE, PARRY, ...) and
    // whether the off hand made it (the line's isOffHand).
    bool hasAmount = false;
    int32_t amount = 0;
    // A damage event's overkill or a heal's overhealing, 0 for none (Era
    // writes -1 for no overkill).
    int32_t overAmount = 0;
    bool critical = false;
    std::string missType;
    bool offHand = false;
    std::string text; // an EMOTE's text, as the chat frame showed it
};

struct Recording {
    std::string log;
    int64_t logStartMs = 0; // the log's first stamp
    int64_t startMs = 0; // the stretch, as stamps
    int64_t endMs = 0;
    int map = -1;
    bool movement = false; // a movement file was read
    // When the first unit is in view: 0, or for a stretch that begins with the
    // log, the movement file's first frame (it opens a frame after the log).
    int32_t firstUnitMs = 0;
    std::vector<Unit> units;
    std::vector<CombatEvent> events; // in time order
    std::vector<GameObject> objects;
    std::vector<GroundArea> areas; // in the order they came into view
    std::vector<GameClock> clock;  // in time order
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

// The player's appearance at `t`: the last that began by then, else its
// first; null for a unit with none.
const Appearance *AppearanceAt(const Unit &unit, int32_t t);

// Whether the unit is dead at `t`.
bool DeadAt(const Unit &unit, int32_t t);

// The unit's health at `t`, null before the log first gives it.
const Health *HealthAt(const Unit &unit, int32_t t);

// The cast the unit began last by `t`, null for none.
const Cast *CastAt(const Unit &unit, int32_t t);

// The unit's raid target mark at `t`, 0 for none.
int MarkAt(const Unit &unit, int32_t t);

// The player's guild at `t`, null before its first PLAYER_INFO.
const Guild *GuildAt(const Unit &unit, int32_t t);
// The player's name with its PvP rank title at `t`, null before its first.
const std::string *PvPNameAt(const Unit &unit, int32_t t);

// The movement file's state, and health and power, of the unit at `t`; null
// before the first, or with no movement file lines for it.
const UnitState *StateAt(const Unit &unit, int32_t t);
const Vitals *VitalsAt(const Unit &unit, int32_t t);

// The aura's stacks at `t`, null when the logs do not give them by then.
const uint32_t *StacksAt(const AuraSpan &aura, int32_t t);

// The game object's fields at `t`; null while it is not in view.
const ObjectLook *ObjectLookAt(const GameObject &object, int32_t t);

// The time of day at `t` (0 midnight, 0.5 noon), from the clock line before
// it (else the first) moved on at its scale. False with no clock lines.
bool TimeOfDayAt(const Recording &recording, int32_t t, float &timeOfDay);

// Loads the stretch [startMs, endMs] (stamps) of a log (a Catalog name). Null
// when the combat log cannot be read. Worker thread safe.
std::unique_ptr<Recording> Load(const std::string &log, int64_t startMs, int64_t endMs);

} // namespace Replay
