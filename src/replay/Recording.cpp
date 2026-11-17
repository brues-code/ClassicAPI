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

// Where a recorded unit is at a time. A player moves in a line from one move
// message to the next: the client sends one at each start, stop and turn and
// a heartbeat every 500 ms while moving, so the line between two is the path
// to within a few yards. A server-driven unit walks its MONSTER_MOVE path at
// one speed over the path's duration, as the client moves it. In the air
// the line is the client's fall arc (Unit::Fall) from the move's fall time
// and speed, bent to land where the next move is.

#include "replay/Recording.h"

#include "Offsets.h"
#include "unit/Fall.h"

#include <algorithm>
#include <cmath>

namespace Replay {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kMinStep = 0.01f; // yards: closer points give no direction

bool Present(const Unit &unit, int32_t t) {
    if (unit.spans.empty())
        return !unit.keys.empty() && unit.keys.front().t <= t;
    return std::any_of(unit.spans.begin(), unit.spans.end(),
                       [t](const Span &s) { return s.from <= t && t < s.to; });
}

// The angle from `a` toward `b` in the game's facing (0 along +x, counter-
// clockwise), or `fallback` when they are too close.
float Heading(float ax, float ay, float bx, float by, float fallback) {
    const float dx = bx - ax;
    const float dy = by - ay;
    if (dx * dx + dy * dy < kMinStep * kMinStep)
        return fallback;
    float angle = std::atan2(dy, dx);
    return angle < 0 ? angle + 2 * kPi : angle;
}

// The facing a fraction `f` of the way from `a` to `b`, the short way round.
float LerpAngle(float a, float b, float f) {
    float d = std::fmod(b - a, 2 * kPi);
    if (d > kPi)
        d -= 2 * kPi;
    else if (d < -kPi)
        d += 2 * kPi;
    float angle = std::fmod(a + d * f, 2 * kPi);
    return angle < 0 ? angle + 2 * kPi : angle;
}

void SplineAt(const Recording &recording, const Unit &unit, const Key &key, int32_t t, int depth,
              Place &place);

// The place from the key in effect at `t` (the last at or before it).
bool KeyPlace(const Recording &recording, const Unit &unit, int32_t t, int depth, Place &place) {
    const auto it = std::upper_bound(unit.keys.begin(), unit.keys.end(), t,
                                     [](int32_t time, const Key &k) { return time < k.t; });
    if (it == unit.keys.begin())
        return false;
    const Key &key = *(it - 1);
    place = {key.x, key.y, key.z, key.facing, 0.0f};
    if (key.kind == KeyKind::Spline) {
        SplineAt(recording, unit, key, t, depth, place);
        return true;
    }
    // A move goes on in a line to the next move; a snap, a stop or a path
    // after it holds the place until then.
    if (it == unit.keys.end() || it->kind != KeyKind::Sample || it->t <= key.t)
        return true;
    const Key &next = *it;
    const float f = static_cast<float>(t - key.t) / static_cast<float>(next.t - key.t);
    place.x = key.x + (next.x - key.x) * f;
    place.y = key.y + (next.y - key.y) * f;
    place.z = key.z + (next.z - key.z) * f;
    place.facing = LerpAngle(key.facing, next.facing, f);
    const float dx = next.x - key.x, dy = next.y - key.y, dz = next.z - key.z;
    if (key.clocked && (key.flags & Offsets::MOVEFLAG_FALLING)) {
        // The client moves an airborne unit at one ground speed along its
        // jump's heading (the line) while it falls the arc. The arc's start
        // is where the key's fall time puts it; the error at the next key
        // is spread along the way, so the unit lands where that key is.
        const bool slow = (key.flags & Offsets::MOVEFLAG_SAFE_FALL) != 0;
        auto drop = [&](int32_t ms) {
            return static_cast<float>(::Unit::Fall::Distance(
                static_cast<float>(key.fallTime + ms) / 1000.0f, key.fallSpeed, slow));
        };
        const float start = key.z + drop(0);
        const float miss = next.z - (start - drop(next.t - key.t));
        place.z = start - drop(t - key.t) + miss * f;
        place.falling = true;
        place.jumped = key.fallSpeed < 0.0f;
        place.speed = std::sqrt(dx * dx + dy * dy) * 1000.0f / static_cast<float>(next.t - key.t);
        return true;
    }
    place.speed = std::sqrt(dx * dx + dy * dy + dz * dz) * 1000.0f / static_cast<float>(next.t - key.t);
    return true;
}

void SplineAt(const Recording &recording, const Unit &unit, const Key &key, int32_t t, int depth,
              Place &place) {
    const Spline &spline = unit.splines[key.spline];
    const std::size_t n = spline.lengths.size();
    const float total = spline.lengths.back();
    const int32_t elapsed = t - key.t;
    const bool moving = spline.durationMs > 0 && elapsed < spline.durationMs && total > kMinStep;
    const float f = spline.durationMs > 0
                        ? std::clamp(static_cast<float>(elapsed) / spline.durationMs, 0.0f, 1.0f)
                        : 1.0f;
    const float along = f * total;
    // The segment the unit is on.
    std::size_t i = 1;
    while (i < n - 1 && spline.lengths[i] < along)
        ++i;
    const float *a = &spline.points[(i - 1) * 3];
    const float *b = &spline.points[i * 3];
    const float segment = spline.lengths[i] - spline.lengths[i - 1];
    const float s = segment > 0 ? (along - spline.lengths[i - 1]) / segment : 1.0f;
    place.x = a[0] + (b[0] - a[0]) * s;
    place.y = a[1] + (b[1] - a[1]) * s;
    place.z = a[2] + (b[2] - a[2]) * s;
    place.facing = Heading(a[0], a[1], b[0], b[1], key.facing);
    place.speed = moving ? total * 1000.0f / spline.durationMs : 0.0f;
    if (moving)
        return;
    // At the end the unit turns as the path says.
    if (spline.faceType == 4) {
        place.facing = spline.faceAngle;
    } else if (spline.faceType == 2) {
        place.facing = Heading(place.x, place.y, spline.facePoint[0], spline.facePoint[1], place.facing);
    } else if (spline.faceType == 3 && spline.faceUnit >= 0 && depth == 0) {
        Place target;
        const Unit &faced = recording.units[spline.faceUnit];
        if (Present(faced, t) && KeyPlace(recording, faced, t, depth + 1, target))
            place.facing = Heading(place.x, place.y, target.x, target.y, place.facing);
    }
}

} // namespace

bool PlaceAt(const Recording &recording, const Unit &unit, int32_t t, Place &place) {
    return Present(unit, t) && KeyPlace(recording, unit, t, 0, place);
}

const Appearance *AppearanceAt(const Unit &unit, int32_t t) {
    if (unit.appearances.empty())
        return nullptr;
    const auto it = std::upper_bound(
        unit.appearances.begin(), unit.appearances.end(), t,
        [](int32_t time, const std::pair<int32_t, Appearance> &a) { return time < a.first; });
    return it == unit.appearances.begin() ? &unit.appearances.front().second : &(it - 1)->second;
}

const Health *HealthAt(const Unit &unit, int32_t t) {
    const auto it = std::upper_bound(
        unit.health.begin(), unit.health.end(), t,
        [](int32_t time, const std::pair<int32_t, Health> &h) { return time < h.first; });
    return it == unit.health.begin() ? nullptr : &(it - 1)->second;
}

bool DeadAt(const Unit &unit, int32_t t) {
    return std::any_of(unit.deaths.begin(), unit.deaths.end(),
                       [t](const Span &s) { return s.from <= t && t < s.to; });
}

const Cast *CastAt(const Unit &unit, int32_t t) {
    const auto it = std::upper_bound(unit.casts.begin(), unit.casts.end(), t,
                                     [](int32_t time, const Cast &c) { return time < c.start; });
    return it == unit.casts.begin() ? nullptr : &*(it - 1);
}

int MarkAt(const Unit &unit, int32_t t) {
    const auto it = std::upper_bound(
        unit.marks.begin(), unit.marks.end(), t,
        [](int32_t time, const std::pair<int32_t, uint8_t> &m) { return time < m.first; });
    return it == unit.marks.begin() ? 0 : (it - 1)->second;
}

const Look *LookAt(const Unit &unit, int32_t t) {
    const auto it = std::upper_bound(
        unit.looks.begin(), unit.looks.end(), t,
        [](int32_t time, const std::pair<int32_t, Look> &l) { return time < l.first; });
    return it == unit.looks.begin() ? nullptr : &(it - 1)->second;
}

} // namespace Replay
