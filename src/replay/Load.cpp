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

// Loading a stretch of a log (Replay::Load). The movement file is read from
// its start, as a unit's look and place at the stretch's start come from
// lines written any time before it; of those, each unit keeps only its last
// key, look and time in view. The lines after the stretch's end are read a
// little way on, so a move that runs past the end has its next key. The
// combat log gives each unit's name, the units that never came into the
// recorder's view, and the map when there is no movement file.

#include "combatlog/Movement.h"
#include "replay/Catalog.h"
#include "replay/Line.h"
#include "replay/Recording.h"

#include "Offsets.h"
#include "spell/Lookup.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace Replay {

namespace {

// How far past the stretch's end the movement file is read.
constexpr int64_t kTailMs = 10000;
// A unit's cast this soon after its death is the death's own spell, not the
// unit acting again.
constexpr int32_t kDeathSpellMs = 1000;
// A UNIT_CAST that begins a cast this close to a SPELL_CAST_START of its spell
// is that cast's: both come from one SMSG_SPELL_START, the log line from the
// message, the UNIT_CAST from the frame after it.
constexpr int32_t kCastMatchMs = 500;
// A cast with no success line that stopped this close to its end went off
// (a cast whose success the log leaves out, as it does for opening a lock):
// the server's message that it went off and the client's cast time end at
// about the same time, apart by the network's delay.
constexpr int32_t kWentOffMs = 300;
// How long after its SPELL_CAST_SUCCESS a channel's UNIT_CAST can come: the
// server sends UNIT_FIELD_CHANNEL_SPELL up to about a second after the
// SMSG_SPELL_GO that starts the channel (spell/Cast.cpp).
constexpr int32_t kChannelLagMs = 2000;

class Loader {
public:
    Loader(Recording &recording) : r_(recording) {}

    void Movement(std::string_view body, int32_t t);
    // A combat log line at `t` (before the stretch when negative).
    void Combat(std::string_view body, int32_t t);
    // The map ZONE_CHANGE gives at the stretch's start, -1 for none.
    int CombatMap() const { return combatMap_; }

private:
    // The unit's index, added when it is new. Adding one moves the others.
    int32_t IndexOf(std::string_view guid);
    // The unit's index, or -1 when it is not a unit or not known yet.
    int32_t Find(std::string_view guid) const;
    // The unit at a combat line's field `i`: before the stretch only a known
    // one (a unit with no place in it is not added for a line before it).
    int32_t UnitAt(const Line::Fields &f, int i, bool before);
    void Aura(const Line::Fields &f, int32_t t, int32_t dest, bool applied);
    void Revive(int32_t unit, int32_t t);
    void AddHealth(int32_t unit, int32_t t, const Health &health);
    void AddMark(int32_t unit, int32_t t, uint32_t raidFlags);
    void AdvancedBlock(const Line::Fields &f, int32_t t, bool before);
    Unit &Get(std::string_view guid) { return r_.units[IndexOf(guid)]; }
    void AddKey(Unit &unit, Key key, Spline *spline = nullptr);
    void AddLook(Unit &unit, int32_t t, const Look &look);
    void AddAppearance(Unit &unit, int32_t t, const Appearance &appearance);
    void AddSpeeds(Unit &unit, int32_t t, const Speeds &speeds);
    void AddCastState(Unit &unit, const CastState &state);
    void Close(Unit &unit, int32_t t);
    void Header(const Line::Fields &f, int32_t t);
    void UnitInfo(const Line::Fields &f, int32_t t);
    void PlayerInfo(const Line::Fields &f, int32_t t);
    void UnitMove(const Line::Fields &f, int32_t t);
    void MonsterMove(const Line::Fields &f, int32_t t);
    void MonsterStop(const Line::Fields &f, int32_t t);
    void UnitSpeed(const Line::Fields &f, int32_t t);
    void UnitCast(const Line::Fields &f, int32_t t);
    void UnitState(const Line::Fields &f, int32_t t);
    void UnitPower(const Line::Fields &f, int32_t t);
    void UnitAuras(const Line::Fields &f, int32_t t);
    void GameObjectInfo(const Line::Fields &f, int32_t t);
    void DynamicObjectInfo(const Line::Fields &f, int32_t t);
    void ObjectGone(std::string_view guid, int32_t t);
    // Adds a value to a timeline; before the stretch only the last counts.
    template <class T>
    static void AddAt(std::vector<std::pair<int32_t, T>> &timeline, int32_t t, T value) {
        if (t < 0 && !timeline.empty() && timeline.back().first < 0)
            timeline.back() = {t, std::move(value)};
        else
            timeline.emplace_back(t, std::move(value));
    }

    Recording &r_;
    std::unordered_map<std::string, int32_t> index_;
    std::unordered_map<std::string, int32_t> objectIndex_; // into r_.objects
    std::unordered_map<std::string, std::size_t> openAreas_; // into r_.areas, while in view
    int combatMap_ = -1;
    // The SPELL_CAST_START each caster has open, by spell: an index into
    // the events.
    std::unordered_map<uint64_t, size_t> openCasts_;
    bool advanced_ = false; // COMBAT_LOG_VERSION's ADVANCED_LOG_ENABLED
};

bool IsUnitGuid(std::string_view guid) {
    return guid.rfind("Player-", 0) == 0 || guid.rfind("Creature-", 0) == 0 ||
           guid.rfind("Pet-", 0) == 0;
}

// The creature ID in Creature-0-0-<map>-0-<id>-<spawn>.
uint32_t CreatureID(std::string_view guid) {
    std::size_t p = 0;
    for (int dash = 0; dash < 5; ++dash) {
        p = guid.find('-', p);
        if (p == std::string_view::npos)
            return 0;
        ++p;
    }
    uint32_t id = 0;
    std::from_chars(guid.data() + p, guid.data() + guid.size(), id);
    return id;
}

int32_t Loader::IndexOf(std::string_view guid) {
    const auto [it, added] =
        index_.try_emplace(std::string(guid), static_cast<int32_t>(r_.units.size()));
    if (added) {
        Unit unit;
        unit.guid = it->first;
        unit.player = guid.rfind("Player-", 0) == 0;
        if (!unit.player)
            unit.creatureID = CreatureID(guid);
        r_.units.push_back(std::move(unit));
    }
    return it->second;
}

// A key, with its path for a Spline key. Before the stretch only the last
// key counts, so a key there replaces the one before it (and that key's
// path, the unit's last).
void Loader::AddKey(Unit &unit, Key key, Spline *spline) {
    const bool replace = key.t < 0 && !unit.keys.empty() && unit.keys.back().t < 0;
    if (replace && unit.keys.back().kind == KeyKind::Spline)
        unit.splines.pop_back();
    if (spline != nullptr) {
        unit.splines.push_back(std::move(*spline));
        key.spline = static_cast<int32_t>(unit.splines.size()) - 1;
    }
    if (replace)
        unit.keys.back() = key;
    else
        unit.keys.push_back(key);
}

void Loader::AddLook(Unit &unit, int32_t t, const Look &look) {
    if (t < 0 && !unit.looks.empty() && unit.looks.back().first < 0)
        unit.looks.back() = {t, look};
    else
        unit.looks.emplace_back(t, look);
}

// The same appearance again is no change: the file writes every unit again
// after a header, and again for a flag the model does not read (AFK, PvP).
void Loader::AddAppearance(Unit &unit, int32_t t, const Appearance &appearance) {
    if (!unit.appearances.empty() &&
        std::memcmp(&unit.appearances.back().second, &appearance, sizeof appearance) == 0)
        return;
    if (t < 0 && !unit.appearances.empty() && unit.appearances.back().first < 0)
        unit.appearances.back() = {t, appearance};
    else
        unit.appearances.emplace_back(t, appearance);
}

void Loader::AddSpeeds(Unit &unit, int32_t t, const Speeds &speeds) {
    if (t < 0 && !unit.speeds.empty() && unit.speeds.back().first < 0)
        unit.speeds.back() = {t, speeds};
    else
        unit.speeds.emplace_back(t, speeds);
}

void Loader::AddCastState(Unit &unit, const CastState &state) {
    if (state.t < 0 && !unit.castStates.empty() && unit.castStates.back().t < 0)
        unit.castStates.back() = state;
    else
        unit.castStates.push_back(state);
}

// The unit leaves view, and with it any cast it had under way. A time in
// view that ended before the stretch is dropped.
void Loader::Close(Unit &unit, int32_t t) {
    if (!unit.castStates.empty() && (unit.castStates.back().spell != 0 || unit.castStates.back().channel != 0)) {
        CastState none;
        none.t = t;
        AddCastState(unit, none);
    }
    if (unit.spans.empty() || unit.spans.back().to != INT32_MAX)
        return;
    unit.spans.back().to = t;
    if (t < 0)
        unit.spans.pop_back();
}

// After a header every unit is written again. The map is the one at the
// stretch's start, or the first one after it: the movement file opens a
// frame after the combat log, so a stretch from the log's start begins
// before the file's first header.
void Loader::Header(const Line::Fields &f, int32_t t) {
    for (Unit &unit : r_.units)
        Close(unit, t);
    for (const GameObject &object : r_.objects)
        ObjectGone(object.guid, t);
    while (!openAreas_.empty())
        ObjectGone(openAreas_.begin()->first, t);
    if (t <= 0 || r_.map < 0)
        r_.map = f.Int(5);
}

void Loader::UnitInfo(const Line::Fields &f, int32_t t) {
    Unit &unit = Get(f[1]);
    if (unit.spans.empty() || unit.spans.back().to != INT32_MAX) {
        // Into view: its place. Later UNIT_INFO places are not keys, as a
        // look change mid-path would stop the path.
        unit.spans.push_back({t, INT32_MAX});
        Key key;
        key.t = t;
        key.kind = KeyKind::Snap;
        key.x = f.Float(2);
        key.y = f.Float(3);
        key.z = f.Float(4);
        key.facing = f.Float(5);
        AddKey(unit, key);
    }
    const Speeds speeds{f.Float(6), f.Float(7), f.Float(8), f.Float(9), f.Float(10)};
    AddSpeeds(unit, t, speeds);
    Look look;
    look.display = static_cast<uint32_t>(f.Int(11));
    look.nativeDisplay = static_cast<uint32_t>(f.Int(12));
    look.mountDisplay = static_cast<uint32_t>(f.Int(13));
    look.scale = f.Float(14);
    look.bytes1 = f.Hex(15);
    look.bytes2 = f.Hex(16);
    for (int i = 0; i < Look::kHands; ++i)
        look.heldDisplay[i] = static_cast<uint32_t>(f.Int(17 + i));
    for (int i = 0; i < Look::kHands * 2; ++i)
        look.heldInfo[i] = f.Hex(20 + i);
    AddLook(unit, t, look);
}

void Loader::PlayerInfo(const Line::Fields &f, int32_t t) {
    Appearance a;
    a.race = static_cast<uint32_t>(f.Int(2));
    a.classId = static_cast<uint32_t>(f.Int(3));
    a.sex = static_cast<uint32_t>(f.Int(4));
    a.skin = static_cast<uint32_t>(f.Int(5));
    a.face = static_cast<uint32_t>(f.Int(6));
    a.hairStyle = static_cast<uint32_t>(f.Int(7));
    a.hairColor = static_cast<uint32_t>(f.Int(8));
    a.facialHair = static_cast<uint32_t>(f.Int(9));
    const uint32_t flags = f.Hex(10);
    a.hideHelm = (flags & Offsets::PLAYER_FLAG_HIDE_HELM) != 0;
    a.hideCloak = (flags & Offsets::PLAYER_FLAG_HIDE_CLOAK) != 0;
    for (int i = 0; i < Appearance::kSlots; ++i) {
        a.items[i] = static_cast<uint32_t>(f.Int(11 + i * 2));
        a.enchants[i] = static_cast<uint32_t>(f.Int(12 + i * 2));
    }
    constexpr int kEmblem = 11 + Appearance::kSlots * 2;
    for (int i = 0; i < 5; ++i)
        a.emblem[i] = f.Int(kEmblem + i);
    AddAppearance(Get(f[1]), t, a);
}

void Loader::UnitSpeed(const Line::Fields &f, int32_t t) {
    Unit &unit = Get(f[1]);
    if (unit.speeds.empty())
        return; // nothing to change before the unit's UNIT_INFO
    Speeds speeds = unit.speeds.back().second;
    const float speed = f.Float(3);
    if (f[2] == "WALK")
        speeds.walk = speed;
    else if (f[2] == "RUN")
        speeds.run = speed;
    else if (f[2] == "RUN_BACK")
        speeds.runBack = speed;
    else if (f[2] == "SWIM")
        speeds.swim = speed;
    else if (f[2] == "SWIM_BACK")
        speeds.swimBack = speed;
    else
        return; // TURN_RATE
    AddSpeeds(unit, t, speeds);
}

void Loader::UnitCast(const Line::Fields &f, int32_t t) {
    CastState state;
    state.t = t;
    state.spell = static_cast<uint32_t>(f.Int(2));
    state.castMs = static_cast<uint32_t>(f.Int(3));
    state.channel = static_cast<uint32_t>(f.Int(4));
    AddCastState(Get(f[1]), state);
}

// A unit GUID's index, -1 for none (0000000000000000) or not a unit.
void Loader::UnitState(const Line::Fields &f, int32_t t) {
    Replay::UnitState state;
    state.target = IsUnitGuid(f[2]) ? IndexOf(f[2]) : -1;
    state.owner = IsUnitGuid(f[3]) ? IndexOf(f[3]) : -1;
    state.flags = f.Hex(4);
    state.level = static_cast<uint32_t>(f.Int(5));
    state.faction = static_cast<uint32_t>(f.Int(6));
    AddAt(Get(f[1]).states, t, state);
}

void Loader::UnitPower(const Line::Fields &f, int32_t t) {
    Vitals vitals;
    vitals.health = static_cast<uint32_t>(f.Int(2));
    vitals.maxHealth = static_cast<uint32_t>(f.Int(3));
    vitals.powerType = static_cast<uint32_t>(f.Int(4));
    vitals.power = static_cast<uint32_t>(f.Int(5));
    vitals.maxPower = static_cast<uint32_t>(f.Int(6));
    AddAt(Get(f[1]).vitals, t, vitals);
}

// spellID, harmful, stacks for each aura, from field 2 on: up to 48 auras,
// more fields than Line::Fields keeps, so read from the rest of the line.
void Loader::UnitAuras(const Line::Fields &f, int32_t t) {
    std::vector<uint32_t> values;
    std::string_view rest = f.Rest(2);
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view field = rest.substr(0, comma);
        uint32_t v = 0;
        std::from_chars(field.data(), field.data() + field.size(), v);
        values.push_back(v);
        rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
    }
    std::vector<AuraSlot> slots;
    for (std::size_t i = 0; i + 2 < values.size(); i += 3) {
        AuraSlot slot;
        slot.spellID = values[i];
        slot.harmful = values[i + 1] != 0;
        slot.stacks = values[i + 2];
        slots.push_back(slot);
    }
    AddAt(Get(f[1]).auraSlots, t, std::move(slots));
}

void Loader::GameObjectInfo(const Line::Fields &f, int32_t t) {
    const auto [it, added] =
        objectIndex_.try_emplace(std::string(f[1]), static_cast<int32_t>(r_.objects.size()));
    if (added) {
        GameObject object;
        object.guid = it->first;
        object.objectID = CreatureID(it->first);
        r_.objects.push_back(std::move(object));
    }
    GameObject &object = r_.objects[it->second];
    if (object.spans.empty() || object.spans.back().to != INT32_MAX)
        object.spans.push_back({t, INT32_MAX});
    ObjectLook look;
    look.owner = IsUnitGuid(f[2]) ? IndexOf(f[2]) : -1;
    look.display = static_cast<uint32_t>(f.Int(3));
    look.type = static_cast<uint32_t>(f.Int(4));
    look.state = static_cast<uint32_t>(f.Int(5));
    look.flags = f.Hex(6);
    look.x = f.Float(7);
    look.y = f.Float(8);
    look.z = f.Float(9);
    look.facing = f.Float(10);
    for (int i = 0; i < 4; ++i)
        look.rotation[i] = f.Float(11 + i);
    look.scale = f.Float(15);
    const std::string name = f.Text(16);
    if (!name.empty())
        object.name = name;
    AddAt(object.looks, t, look);
}

void Loader::DynamicObjectInfo(const Line::Fields &f, int32_t t) {
    const std::string guid(f[1]);
    if (openAreas_.count(guid) != 0)
        return;
    GroundArea area;
    area.guid = guid;
    area.caster = IsUnitGuid(f[2]) ? IndexOf(f[2]) : -1;
    area.spellID = static_cast<uint32_t>(f.Int(3));
    area.bytes = f.Hex(4);
    area.radius = f.Float(5);
    area.x = f.Float(6);
    area.y = f.Float(7);
    area.z = f.Float(8);
    area.facing = f.Float(9);
    area.span.from = t;
    openAreas_.emplace(guid, r_.areas.size());
    r_.areas.push_back(std::move(area));
}

// A game object or ground area leaves view. One whose time in view ended
// before the stretch is dropped.
void Loader::ObjectGone(std::string_view guid, int32_t t) {
    const std::string key(guid);
    if (const auto it = objectIndex_.find(key); it != objectIndex_.end()) {
        GameObject &object = r_.objects[it->second];
        if (object.spans.empty() || object.spans.back().to != INT32_MAX)
            return;
        object.spans.back().to = t;
        if (t < 0)
            object.spans.pop_back();
        return;
    }
    const auto open = openAreas_.find(key);
    if (open == openAreas_.end())
        return;
    const std::size_t i = open->second;
    openAreas_.erase(open);
    r_.areas[i].span.to = t;
    if (t >= 0)
        return;
    r_.areas.erase(r_.areas.begin() + static_cast<std::ptrdiff_t>(i));
    for (auto &[other, index] : openAreas_) {
        if (index > i)
            --index;
    }
}

void Loader::UnitMove(const Line::Fields &f, int32_t t) {
    Key key;
    key.t = t;
    // A teleport puts the unit there; it does not walk there.
    key.kind = f[2] == "TELEPORT" || f[2] == "TELEPORT_ACK" ? KeyKind::Snap : KeyKind::Sample;
    key.clocked = true;
    const std::string_view clock = f[4];
    std::from_chars(clock.data(), clock.data() + clock.size(), key.clock);
    key.flags = f.Hex(3);
    key.fallTime = static_cast<uint32_t>(f.Int(10));
    key.fallSpeed = f.Float(11);
    key.x = f.Float(5);
    key.y = f.Float(6);
    key.z = f.Float(7);
    key.facing = f.Float(8);
    AddKey(Get(f[1]), key);
}

void Loader::MonsterMove(const Line::Fields &f, int32_t t) {
    // A path on a transport is in the transport's frame, which the file
    // does not place.
    if (f[2] != "0000000000000000")
        return;
    Spline spline;
    spline.durationMs = f.Int(5);
    spline.faceType = f.Int(6);
    spline.facePoint[0] = f.Float(7);
    spline.facePoint[1] = f.Float(8);
    spline.facePoint[2] = f.Float(9);
    if (spline.faceType == 3 && IsUnitGuid(f[10]))
        spline.faceUnit = IndexOf(f[10]);
    spline.faceAngle = f.Float(11);
    const int count = f.Int(12);
    // The points: every field from the 14th, three to a point.
    std::string_view rest = f.Rest(13);
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view field = rest.substr(0, comma);
        float v = 0.0f;
        std::from_chars(field.data(), field.data() + field.size(), v);
        spline.points.push_back(v);
        rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
    }
    if (count < 2 || spline.points.size() != static_cast<std::size_t>(count) * 3)
        return;
    float along = 0.0f;
    spline.lengths.push_back(0.0f);
    for (int i = 1; i < count; ++i) {
        const float *a = &spline.points[(i - 1) * 3];
        const float *b = &spline.points[i * 3];
        along += std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) +
                           (b[2] - a[2]) * (b[2] - a[2]));
        spline.lengths.push_back(along);
    }
    Unit &mover = Get(f[1]);
    Key key;
    key.t = t;
    key.kind = KeyKind::Spline;
    key.x = spline.points[0];
    key.y = spline.points[1];
    key.z = spline.points[2];
    key.facing = mover.keys.empty() ? 0.0f : mover.keys.back().facing;
    AddKey(mover, key, &spline);
}

void Loader::MonsterStop(const Line::Fields &f, int32_t t) {
    if (f[2] != "0000000000000000")
        return;
    Unit &unit = Get(f[1]);
    Key key;
    key.t = t;
    key.kind = KeyKind::Snap;
    key.x = f.Float(4);
    key.y = f.Float(5);
    key.z = f.Float(6);
    key.facing = unit.keys.empty() ? 0.0f : unit.keys.back().facing;
    AddKey(unit, key);
}

void Loader::Movement(std::string_view body, int32_t t) {
    const Line::Fields f(body);
    if (f[0] == "UNIT_MOVE")
        UnitMove(f, t);
    else if (f[0] == "MONSTER_MOVE")
        MonsterMove(f, t);
    else if (f[0] == "UNIT_INFO")
        UnitInfo(f, t);
    else if (f[0] == "PLAYER_INFO")
        PlayerInfo(f, t);
    else if (f[0] == "UNIT_GONE")
        Close(Get(f[1]), t);
    else if (f[0] == "MONSTER_STOP")
        MonsterStop(f, t);
    else if (f[0] == "UNIT_SPEED")
        UnitSpeed(f, t);
    else if (f[0] == "UNIT_CAST")
        UnitCast(f, t);
    else if (f[0] == "UNIT_STATE")
        UnitState(f, t);
    else if (f[0] == "UNIT_POWER")
        UnitPower(f, t);
    else if (f[0] == "UNIT_AURAS")
        UnitAuras(f, t);
    else if (f[0] == "UNIT_NAME")
        Get(f[1]).cachedName = f.Text(2);
    else if (f[0] == "GAMEOBJECT_INFO")
        GameObjectInfo(f, t);
    else if (f[0] == "DYNAMICOBJECT_INFO")
        DynamicObjectInfo(f, t);
    else if (f[0] == "OBJECT_GONE")
        ObjectGone(f[1], t);
    else if (f[0] == "MOVEMENT_LOG_VERSION")
        Header(f, t);
}

// A combat line names its source and its dest (fields 1-2 and 5-6), and a
// ZONE_CHANGE gives the map: the last before the stretch, or the first in it
// (one follows the log's header, on the same stamp).
int32_t Loader::Find(std::string_view guid) const {
    const auto it = index_.find(std::string(guid));
    return it != index_.end() ? it->second : -1;
}

int32_t Loader::UnitAt(const Line::Fields &f, int i, bool before) {
    if (!IsUnitGuid(f[i]))
        return -1;
    return before ? Find(f[i]) : IndexOf(f[i]);
}

// SPELL_AURA_APPLIED opens a span on the destination, SPELL_AURA_REMOVED
// ends the last open one of that spell (its source is often nil), and
// SPELL_AURA_REFRESH opens one only when none is open.
void Loader::Aura(const Line::Fields &f, int32_t t, int32_t dest, bool applied) {
    if (dest < 0)
        return;
    std::vector<AuraSpan> &auras = r_.units[dest].auras;
    const auto spellID = static_cast<uint32_t>(f.Int(9));
    auto open = std::find_if(auras.rbegin(), auras.rend(),
                             [&](const AuraSpan &a) { return a.spellID == spellID && a.to == INT32_MAX; });
    if (!applied) {
        if (open != auras.rend())
            open->to = t;
        return;
    }
    if (f[0] == "SPELL_AURA_REFRESH" && open != auras.rend())
        return;
    AuraSpan span;
    span.spellID = spellID;
    span.debuff = f[12] == "DEBUFF";
    span.caster = Find(f[1]);
    span.from = t;
    auras.push_back(span);
}

// Before the stretch only the last sample counts.
void Loader::AddHealth(int32_t unit, int32_t t, const Health &health) {
    if (unit < 0)
        return;
    auto &samples = r_.units[unit].health;
    if (t < 0 && !samples.empty() && samples.back().first < 0)
        samples.back() = {t, health};
    else
        samples.emplace_back(t, health);
}

// The mark in a line's raid flags (COMBATLOG_OBJECT_RAIDTARGET1..8, bits 0
// to 7), when it changes. 0x80000000 is a line's "no unit". Before the
// stretch only the last counts.
void Loader::AddMark(int32_t unit, int32_t t, uint32_t raidFlags) {
    if (unit < 0 || (raidFlags & 0x80000000u) != 0)
        return;
    uint8_t mark = 0;
    for (uint8_t bit = 0; bit < 8; ++bit)
        if (raidFlags & (1u << bit)) {
            mark = bit + 1;
            break;
        }
    auto &marks = r_.units[unit].marks;
    if (!marks.empty() && marks.back().second == mark)
        return;
    if (t < 0 && !marks.empty() && marks.back().first < 0)
        marks.back() = {t, mark};
    else
        marks.emplace_back(t, mark);
}

// The subevents Era gives an advanced block; it follows the spell prefix,
// or the unit parts on a line without one (a swing, environmental damage).
bool HasAdvancedBlock(std::string_view type) {
    static const std::string_view kTypes[] = {
        "SPELL_CAST_SUCCESS",  "SWING_DAMAGE",          "SWING_DAMAGE_LANDED", "SPELL_DAMAGE",
        "SPELL_PERIODIC_DAMAGE", "RANGE_DAMAGE",        "DAMAGE_SHIELD",       "ENVIRONMENTAL_DAMAGE",
        "SPELL_HEAL",          "SPELL_PERIODIC_HEAL",   "SPELL_ENERGIZE",      "SPELL_PERIODIC_ENERGIZE",
        "SPELL_DRAIN",         "SPELL_PERIODIC_DRAIN",  "SPELL_LEECH",         "SPELL_PERIODIC_LEECH",
    };
    return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes);
}

// Where a line's suffix starts: after the unit parts, the spell prefix and
// the advanced block. Environmental damage puts its type first.
int SuffixAt(std::string_view type, bool advanced) {
    const bool spellPrefix = type.rfind("SWING_", 0) != 0 && type.rfind("ENVIRONMENTAL_", 0) != 0;
    return (spellPrefix ? 12 : 9) + (advanced && HasAdvancedBlock(type) ? 18 : 0) +
           (type.rfind("ENVIRONMENTAL_", 0) == 0 ? 1 : 0);
}

bool EndsWith(std::string_view text, std::string_view end) {
    return text.size() >= end.size() && text.substr(text.size() - end.size()) == end;
}

// The block's unit (infoGUID) and its current and max health; a unit the
// writer could not resolve has a block of zeros.
void Loader::AdvancedBlock(const Line::Fields &f, int32_t t, bool before) {
    if (!advanced_ || !HasAdvancedBlock(f[0]))
        return;
    const int at = f[0].rfind("SWING_", 0) == 0 || f[0].rfind("ENVIRONMENTAL_", 0) == 0 ? 9 : 12;
    const int32_t unit = UnitAt(f, at, before);
    const Health health{f.Int(at + 2), f.Int(at + 3)};
    if (health.max > 0)
        AddHealth(unit, t, health);
}

// A dead unit that casts or attacks, or is resurrected, lives again.
void Loader::Revive(int32_t unit, int32_t t) {
    if (unit < 0)
        return;
    std::vector<Span> &deaths = r_.units[unit].deaths;
    if (!deaths.empty() && deaths.back().to == INT32_MAX)
        deaths.back().to = t;
}

void Loader::Combat(std::string_view body, int32_t t) {
    const bool before = t < 0;
    if (Line::Is(body, "ZONE_CHANGE")) {
        if (before || combatMap_ < 0)
            combatMap_ = Line::Fields(body).Int(1);
        return;
    }
    if (Line::Is(body, "COMBAT_LOG_VERSION")) {
        const Line::Fields f(body);
        advanced_ = f[2] == "ADVANCED_LOG_ENABLED" && f.Int(3) == 1;
        return;
    }
    if (Line::Is(body, "ENCOUNTER_START") ||
        Line::Is(body, "ENCOUNTER_END") || Line::Is(body, "MAP_CHANGE"))
        return;
    const Line::Fields f(body);
    const int32_t source = UnitAt(f, 1, before);
    // An EMOTE names its source and its target with no flags between them.
    const bool emote = f[0] == "EMOTE";
    const int32_t dest = UnitAt(f, emote ? 3 : 5, before);
    if (!before) {
        for (const int32_t i : {1, emote ? 3 : 5}) {
            const int32_t unit = i == 1 ? source : dest;
            if (unit >= 0 && f.Quoted(i + 1) && r_.units[unit].name.empty())
                r_.units[unit].name = f.Text(i + 1);
        }
    }
    const std::string_view type = f[0];
    AdvancedBlock(f, t, before);
    if (!emote) {
        AddMark(source, t, f.Hex(4));
        AddMark(dest, t, f.Hex(8));
    }
    if (type == "UNIT_DIED") {
        if (dest >= 0) {
            Revive(dest, t);
            r_.units[dest].deaths.push_back({t, INT32_MAX});
            const auto &samples = r_.units[dest].health;
            AddHealth(dest, t, {0, samples.empty() ? 0 : samples.back().second.max});
        }
    } else if (type == "SPELL_RESURRECT") {
        Revive(dest, t);
    } else if (type == "SPELL_CAST_START" || type.rfind("SWING_", 0) == 0 || type.rfind("RANGE_", 0) == 0) {
        // Its own cast or attack; not a tick of an aura it left behind.
        Revive(source, t);
    } else if (type == "SPELL_CAST_SUCCESS" && source >= 0) {
        // A spell the death itself casts from the corpse comes right after
        // it (a Ravaged Cadaver's Summon Broken Cadaver, 2 ms later).
        const std::vector<Span> &deaths = r_.units[source].deaths;
        if (deaths.empty() || t - deaths.back().from >= kDeathSpellMs)
            Revive(source, t);
    }
    if (type == "SPELL_AURA_APPLIED" || type == "SPELL_AURA_REFRESH")
        Aura(f, t, dest, true);
    else if (type == "SPELL_AURA_REMOVED")
        Aura(f, t, dest, false);
    if (before)
        return;
    CombatEvent e;
    e.t = t;
    e.type = std::string(type);
    e.source = source;
    e.dest = dest;
    if (type.rfind("SPELL_", 0) == 0 || type.rfind("RANGE_", 0) == 0 || type.rfind("DAMAGE_", 0) == 0)
        e.spellID = static_cast<uint32_t>(f.Int(9));
    e.debuff = type.rfind("SPELL_AURA_", 0) == 0 && f[12] == "DEBUFF";
    if (emote)
        e.text = std::string(f.Rest(5));
    // Damage: amount, baseAmount, overkill, school, resisted, blocked,
    // absorbed, critical; heals: amount, baseAmount, overhealing, absorbed,
    // critical; misses: missType first.
    // The damage suffix also follows DAMAGE_SHIELD (Thorns) and DAMAGE_SPLIT,
    // whose names do not end in _DAMAGE.
    const int suffix = SuffixAt(type, advanced_);
    if (EndsWith(type, "_DAMAGE") || type == "DAMAGE_SHIELD" || type == "DAMAGE_SPLIT") {
        e.hasAmount = true;
        e.amount = f.Int(suffix);
        e.critical = f[suffix + 7] == "1";
    } else if (EndsWith(type, "_HEAL")) {
        e.hasAmount = true;
        e.amount = f.Int(suffix);
        e.critical = f[suffix + 4] == "1";
    } else if (EndsWith(type, "_MISSED")) {
        e.missType = std::string(f[suffix]);
    }
    // A cast's start is open until the caster's success or failure of the
    // same spell, a SPELL_INTERRUPT of that spell on the caster, or the
    // caster's death.
    const auto castKey = [](int32_t unit, uint32_t spell) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(unit)) << 32) | spell;
    };
    const uint64_t key = castKey(source, e.spellID);
    if (type == "SPELL_CAST_START" && source >= 0) {
        openCasts_[key] = r_.events.size();
    } else if ((type == "SPELL_CAST_SUCCESS" || type == "SPELL_CAST_FAILED") && source >= 0) {
        const auto open = openCasts_.find(key);
        if (open != openCasts_.end()) {
            CombatEvent &start = r_.events[open->second];
            start.castEnd = t;
            start.castFailed = type == "SPELL_CAST_FAILED";
            e.endsCast = true;
            openCasts_.erase(open);
        }
    } else if (type == "SPELL_INTERRUPT" && dest >= 0) {
        e.extraSpellID = static_cast<uint32_t>(f.Int(suffix));
        const auto open = openCasts_.find(castKey(dest, e.extraSpellID));
        if (open != openCasts_.end()) {
            CombatEvent &start = r_.events[open->second];
            start.castEnd = t;
            start.castFailed = true;
            start.interrupter = source;
            openCasts_.erase(open);
        }
    } else if (type == "UNIT_DIED" && dest >= 0) {
        for (auto open = openCasts_.begin(); open != openCasts_.end();) {
            if (static_cast<int32_t>(open->first >> 32) != dest) {
                ++open;
                continue;
            }
            CombatEvent &start = r_.events[open->second];
            start.castEnd = t;
            start.castFailed = true;
            open = openCasts_.erase(open);
        }
    }
    r_.events.push_back(std::move(e));
}

constexpr uint32_t kDirections = Offsets::MOVEFLAG_FORWARD | Offsets::MOVEFLAG_BACKWARD |
                                 Offsets::MOVEFLAG_STRAFE_LEFT | Offsets::MOVEFLAG_STRAFE_RIGHT;

// The speed the client moves a unit at with these movement flags, 0 when it
// stands: CMovement::GetCurrentSpeed (0x007C4C90). Swimming takes the swim
// speed, the slower swim-back one backward; walk mode the slower of walk and
// run; otherwise the run speed, the slower run-back one backward.
float SpeedFor(uint32_t flags, const Speeds &s) {
    if ((flags & kDirections) == 0)
        return 0.0f;
    const bool back = (flags & Offsets::MOVEFLAG_BACKWARD) != 0;
    if (flags & Offsets::MOVEFLAG_SWIMMING)
        return back ? std::min(s.swimBack, s.swim) : s.swim;
    if (flags & Offsets::MOVEFLAG_WALK_MODE)
        return std::min(s.walk, s.run);
    return back ? std::min(s.runBack, s.run) : s.run;
}

const Speeds *SpeedsAt(const Unit &unit, int32_t t) {
    const auto it = std::upper_bound(
        unit.speeds.begin(), unit.speeds.end(), t,
        [](int32_t time, const std::pair<int32_t, Speeds> &s) { return time < s.first; });
    return it == unit.speeds.begin() ? nullptr : &(it - 1)->second;
}

// How long a player took from move `a` to move `b`, in milliseconds, or -1
// when the moves do not say. In the air the fall time says (the mover's own
// count since the jump). On the ground the unit moved at the speed its flags
// pick, over the distance on the ground (swimming, through the water): over
// 206 runs of a Stormwind log, ground distance over the clock gave 6.99 to
// 7.01 yards a second at run speed 7, the full distance 7.02 to 7.10. A
// standing unit says nothing about how long it stood, nor a teleport.
double Duration(const Unit &unit, const Key &a, const Key &b) {
    if (b.kind != KeyKind::Sample)
        return -1.0;
    if (a.flags & Offsets::MOVEFLAG_FALLING)
        return b.fallTime >= a.fallTime ? static_cast<double>(b.fallTime - a.fallTime) : -1.0;
    const Speeds *speeds = SpeedsAt(unit, a.t);
    const float speed = speeds != nullptr ? SpeedFor(a.flags, *speeds) : 0.0f;
    if (speed <= 0.0f)
        return -1.0;
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double dz = (a.flags & Offsets::MOVEFLAG_SWIMMING) ? b.z - a.z : 0.0;
    return std::sqrt(dx * dx + dy * dy + dz * dz) * 1000.0 / speed;
}

// How much slower than the clocks a stretch's durations run (`along`, from
// the stretch's first move), when a long stretch shows it: our client can hold
// a stale speed for a unit (one mounted player at 11.2 yards a second, the
// jump's own speed, against 7.56 here; the client corrects the drift at
// each heartbeat). The clocks are only ever late, so the true times lie on a
// line under all of them; the one that fits best is the lower hull's edge
// over the middle distance. 1 for a short stretch, or one within 10%.
double SpeedScale(const std::vector<double> &along, const std::vector<double> &times,
                  std::size_t first, std::size_t last) {
    constexpr double kMinMs = 3000.0;
    constexpr std::size_t kMinMoves = 6;
    if (last - first + 1 < kMinMoves || along[last] < kMinMs)
        return 1.0;
    std::vector<std::pair<double, double>> hull; // (along, time), lower
    double mean = 0.0;
    for (std::size_t m = first; m <= last; ++m) {
        const std::pair<double, double> p{along[m], times[m]};
        mean += along[m];
        while (hull.size() >= 2) {
            const auto &a = hull[hull.size() - 2], &b = hull.back();
            if ((b.first - a.first) * (p.second - a.second) -
                    (b.second - a.second) * (p.first - a.first) > 0.0)
                break;
            hull.pop_back();
        }
        hull.push_back(p);
    }
    mean /= static_cast<double>(last - first + 1);
    for (std::size_t i = 1; i < hull.size(); ++i) {
        const auto &a = hull[i - 1], &b = hull[i];
        if (a.first <= mean && mean <= b.first && b.first > a.first) {
            const double scale = (b.second - a.second) / (b.first - a.first);
            return scale > 0.5 && scale < 2.0 && std::abs(scale - 1.0) > 0.1 ? scale : 1.0;
        }
    }
    return 1.0;
}

// Times a unit's moves. Their stamps are bunched: the server sends relayed
// moves in batches, so two sent 500 ms apart can arrive 1 ms apart. Their
// clock (the status time, the server's when it got the mover's message) is
// bunched too, less: a mover's messages reach the server in bursts (one
// player's 11 start and stop messages in one packet, one clock). Neither is
// ever early, only late. What is exact is the place in each, and while the
// unit moves, how long it took from one to the next (Duration). So each
// stretch of moves with known durations has a fixed shape in time, and only
// its start is open: the start that puts no move before its clock and one
// exactly on it, the move that came soonest (SpeedScale corrects a stale
// speed first). A stretch also ends before the
// next one starts, so one that would overlap the next moves earlier whole.
// A key that is not the unit's own move (a path the server moves it on, a
// Charge) ends a stretch. The clock maps onto the stamps the same way, by
// the smallest gap; it is the mover's or the server's, so each unit maps its
// own, and it wraps at 2^32 ms, so it is unwrapped from the unit's first
// move on.
void Retime(Unit &unit) {
    std::vector<std::size_t> moves; // the clocked keys, in order
    std::vector<int64_t> clocks;
    int64_t clock = 0;
    int64_t offset = INT64_MAX;
    for (std::size_t i = 0; i < unit.keys.size(); ++i) {
        const Key &key = unit.keys[i];
        if (!key.clocked)
            continue;
        if (!moves.empty())
            clock += static_cast<int32_t>(key.clock - unit.keys[moves.back()].clock);
        moves.push_back(i);
        clocks.push_back(clock);
        offset = std::min(offset, key.t - clock);
    }
    if (moves.empty())
        return;
    // The clock, on the stamps' time.
    std::vector<double> times(moves.size());
    for (std::size_t m = 0; m < moves.size(); ++m)
        times[m] = static_cast<double>(clocks[m] + offset);
    // Each stretch with known durations, placed by its soonest move.
    struct Stretch {
        std::size_t first, last;
        double start;
    };
    std::vector<Stretch> stretches;
    std::vector<double> along(moves.size());
    for (std::size_t first = 0; first < moves.size();) {
        std::size_t last = first;
        along[first] = 0.0;
        double start = times[first];
        while (last + 1 < moves.size() && moves[last + 1] == moves[last] + 1) {
            const double d =
                Duration(unit, unit.keys[moves[last]], unit.keys[moves[last + 1]]);
            if (d < 0.0)
                break;
            ++last;
            along[last] = along[last - 1] + d;
        }
        const double scale = SpeedScale(along, times, first, last);
        for (std::size_t m = first; m <= last; ++m) {
            along[m] *= scale;
            start = std::min(start, times[m] - along[m]);
        }
        stretches.push_back({first, last, start});
        first = last + 1;
    }
    // From the last back, each ends by the time the next starts.
    double next = INFINITY;
    for (auto it = stretches.rbegin(); it != stretches.rend(); ++it) {
        it->start = std::min(it->start, next - along[it->last]);
        next = it->start;
        for (std::size_t m = it->first; m <= it->last; ++m)
            times[m] = it->start + along[m];
    }
    for (std::size_t m = 0; m < moves.size(); ++m)
        unit.keys[moves[m]].t = static_cast<int32_t>(std::llround(times[m]));
    // A move can now come before a key without a clock (the unit's coming
    // into view, a path) that was stamped after it.
    std::stable_sort(unit.keys.begin(), unit.keys.end(),
                     [](const Key &a, const Key &b) { return a.t < b.t; });
}

// The unit that interrupted `caster`'s cast stopped at `stop`: the source of a
// spell with an interrupt effect that went off on the caster as the cast
// stopped, the nearest. The server writes no execute log, so no
// SPELL_INTERRUPT, for an interrupt that also deals damage (tortoise-wow
// Spell::HandleImmediate drops it for SCHOOL_DAMAGE): Kick, Pummel, Shield
// Bash and Earth Shock leave only their success line, on the same stamp as
// the stop. -1 for none.
int32_t Interrupter(const Recording &r, int32_t caster, int32_t stop) {
    int32_t found = -1, nearest = kCastMatchMs + 1;
    auto it = std::lower_bound(r.events.begin(), r.events.end(), stop - kCastMatchMs,
                               [](const CombatEvent &e, int32_t t) { return e.t < t; });
    for (; it != r.events.end() && it->t <= stop + kCastMatchMs; ++it) {
        if (it->type != "SPELL_CAST_SUCCESS" || it->dest != caster || it->source < 0 ||
            std::abs(it->t - stop) >= nearest)
            continue;
        const uint8_t *spell = Spell::Lookup::RecordForID(static_cast<int>(it->spellID));
        if (Spell::Lookup::HasEffect(spell, Offsets::SPELL_EFFECT_INTERRUPT_CAST)) {
            found = it->source;
            nearest = std::abs(it->t - stop);
        }
    }
    return found;
}

// The unit's cast bars. A cast is a SPELL_CAST_START until its caster's
// success or failure of the spell, a SPELL_INTERRUPT of it, or the caster's
// death. With no such line the cast stopped when
// UNIT_CAST says the unit stopped casting it: before its end it was
// interrupted (the logs write no line for another unit's interrupted cast),
// and the start gets that stop as its end. A channel is a UNIT_CAST's channel
// spell, from the SPELL_CAST_SUCCESS that began it until the spell goes.
void BuildCasts(Recording &r) {
    for (CombatEvent &e : r.events) {
        if (e.type != "SPELL_CAST_START" || e.source < 0)
            continue;
        Unit &unit = r.units[e.source];
        const std::vector<CastState> &states = unit.castStates;
        auto begun = std::lower_bound(states.begin(), states.end(), e.t - kCastMatchMs,
                                      [](const CastState &s, int32_t t) { return s.t < t; });
        while (begun != states.end() && begun->t <= e.t + kCastMatchMs && begun->spell != e.spellID)
            ++begun;
        uint32_t castMs = 0;
        int32_t stopped = -1;
        if (begun != states.end() && begun->t <= e.t + kCastMatchMs) {
            castMs = begun->castMs;
            const auto stop = std::find_if(begun + 1, states.end(),
                                           [&](const CastState &s) { return s.spell != e.spellID; });
            if (stop != states.end())
                stopped = stop->t;
        }
        Cast cast;
        cast.spellID = e.spellID;
        cast.start = e.t;
        if (e.castEnd >= 0) {
            cast.stop = e.castEnd;
            cast.end = e.castFailed && castMs != 0 ? e.t + static_cast<int32_t>(castMs) : e.castEnd;
            cast.interrupted = e.castFailed;
            cast.interrupter = e.interrupter;
        } else if (stopped >= 0) {
            cast.stop = stopped;
            cast.end = e.t + static_cast<int32_t>(castMs);
            if (stopped >= cast.end - kWentOffMs)
                cast.end = stopped;
            else
                cast.interrupted = true;
            e.castEnd = stopped;
            e.castFailed = cast.interrupted;
        } else {
            continue; // the logs do not say when it stopped
        }
        if (cast.interrupted && cast.interrupter < 0)
            cast.interrupter = Interrupter(r, e.source, cast.stop);
        unit.casts.push_back(cast);
    }
    for (std::size_t i = 0; i < r.units.size(); ++i) {
        Unit &unit = r.units[i];
        const auto channel = [&](uint32_t spell, int32_t from, int32_t to) {
            Cast cast;
            cast.spellID = spell;
            cast.start = from;
            cast.stop = to;
            cast.channel = true;
            auto it = std::lower_bound(r.events.begin(), r.events.end(), from - kChannelLagMs,
                                       [](const CombatEvent &e, int32_t t) { return e.t < t; });
            for (; it != r.events.end() && it->t <= from; ++it)
                if (it->type == "SPELL_CAST_SUCCESS" && it->source == static_cast<int32_t>(i) &&
                    it->spellID == spell)
                    cast.start = it->t;
            unit.casts.push_back(cast);
        };
        uint32_t open = 0;
        int32_t from = 0;
        for (const CastState &state : unit.castStates) {
            if (state.channel == open)
                continue;
            if (open != 0)
                channel(open, from, state.t);
            open = state.channel;
            from = state.t;
        }
        if (open != 0)
            channel(open, from, INT32_MAX);
        std::stable_sort(unit.casts.begin(), unit.casts.end(),
                         [](const Cast &a, const Cast &b) { return a.start < b.start; });
    }
}

} // namespace

std::unique_ptr<Recording> Load(const std::string &log, int64_t startMs, int64_t endMs) {
    auto recording = std::make_unique<Recording>();
    recording->log = log;
    recording->startMs = startMs;
    recording->endMs = endMs;
    Loader loader(*recording);

    // A movement file in another format is not read.
    int version = 0;
    Line::ForEach(Catalog::MovementPath(log), [&](std::string_view line) {
        int64_t ms;
        std::string_view body;
        if (Line::Stamp(line, ms, body) && Line::Is(body, "MOVEMENT_LOG_VERSION"))
            version = Line::Fields(body).Int(1);
        return false;
    });
    recording->movement = version == CombatLog::Movement::kVersion &&
                          Line::ForEach(Catalog::MovementPath(log), [&](std::string_view line) {
        int64_t ms;
        std::string_view body;
        if (!Line::Stamp(line, ms, body))
            return true;
        if (ms > endMs + kTailMs)
            return false;
        loader.Movement(body, static_cast<int32_t>(ms - startMs));
        return true;
    });

    for (Unit &unit : recording->units)
        Retime(unit);
    // Objects that left view before the stretch.
    auto &objects = recording->objects;
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [](const GameObject &object) { return object.spans.empty(); }),
                  objects.end());

    const bool read = Line::ForEach(Catalog::CombatPath(log), [&](std::string_view line) {
        int64_t ms;
        std::string_view body;
        if (!Line::Stamp(line, ms, body))
            return true;
        if (recording->logStartMs == 0)
            recording->logStartMs = ms;
        if (ms > endMs)
            return false;
        loader.Combat(body, static_cast<int32_t>(ms - startMs));
        return true;
    });
    if (recording->map < 0)
        recording->map = loader.CombatMap();
    BuildCasts(*recording);
    // A corpse leaving view ends its death: a creature can come back under
    // its GUID (a respawn), new to view.
    for (Unit &unit : recording->units)
        for (Span &death : unit.deaths)
            for (const Span &seen : unit.spans)
                if (seen.from <= death.from && death.from < seen.to && seen.to < death.to)
                    death.to = seen.to;
    return read ? std::move(recording) : nullptr;
}

} // namespace Replay
