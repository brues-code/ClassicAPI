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

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Replay {

namespace {

// How far past the stretch's end the movement file is read.
constexpr int64_t kTailMs = 10000;

class Loader {
public:
    Loader(Recording &recording) : r_(recording) {}

    void Movement(std::string_view body, int32_t t);
    void Combat(std::string_view body, bool before);
    // The map ZONE_CHANGE gives at the stretch's start, -1 for none.
    int CombatMap() const { return combatMap_; }

private:
    // The unit's index, added when it is new. Adding one moves the others.
    int32_t IndexOf(std::string_view guid);
    Unit &Get(std::string_view guid) { return r_.units[IndexOf(guid)]; }
    void AddKey(Unit &unit, Key key, Spline *spline = nullptr);
    void AddLook(Unit &unit, int32_t t, const Look &look);
    void AddAppearance(Unit &unit, int32_t t, const Appearance &appearance);
    void AddSpeeds(Unit &unit, int32_t t, const Speeds &speeds);
    void Close(Unit &unit, int32_t t);
    void Header(const Line::Fields &f, int32_t t);
    void UnitInfo(const Line::Fields &f, int32_t t);
    void PlayerInfo(const Line::Fields &f, int32_t t);
    void UnitMove(const Line::Fields &f, int32_t t);
    void MonsterMove(const Line::Fields &f, int32_t t);
    void MonsterStop(const Line::Fields &f, int32_t t);
    void UnitSpeed(const Line::Fields &f, int32_t t);

    Recording &r_;
    std::unordered_map<std::string, int32_t> index_;
    int combatMap_ = -1;
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

// The unit leaves view. A time in view that ended before the stretch is
// dropped.
void Loader::Close(Unit &unit, int32_t t) {
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
    else if (f[0] == "MOVEMENT_LOG_VERSION")
        Header(f, t);
}

// A combat line names its source and its dest (fields 1-2 and 5-6), and a
// ZONE_CHANGE gives the map: the last before the stretch, or the first in it
// (one follows the log's header, on the same stamp).
void Loader::Combat(std::string_view body, bool before) {
    if (Line::Is(body, "ZONE_CHANGE")) {
        if (before || combatMap_ < 0)
            combatMap_ = Line::Fields(body).Int(1);
        return;
    }
    if (before)
        return;
    const Line::Fields f(body);
    for (const int i : {1, 5}) {
        if (!IsUnitGuid(f[i]) || !f.Quoted(i + 1))
            continue;
        Unit &unit = Get(f[i]);
        if (unit.name.empty())
            unit.name = f.Text(i + 1);
    }
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

    const bool read = Line::ForEach(Catalog::CombatPath(log), [&](std::string_view line) {
        int64_t ms;
        std::string_view body;
        if (!Line::Stamp(line, ms, body))
            return true;
        if (recording->logStartMs == 0)
            recording->logStartMs = ms;
        if (ms > endMs)
            return false;
        loader.Combat(body, ms < startMs);
        return true;
    });
    if (recording->map < 0)
        recording->map = loader.CombatMap();
    return read ? std::move(recording) : nullptr;
}

} // namespace Replay
