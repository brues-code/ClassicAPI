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

// A scene camera's shakes: the world camera's (benilla uicamera/effects,
// spell/shakes) on a scene's camera, step for step:
//
//   1. Adding one (CameraAddShake 0x00511D40): a CameraShakes row by id, its
//      amplitude times 1/36 once (yards), kept with the position and the
//      spawn time (0x00511C60). An id outside the table adds nothing.
//   2. A group (PlaySpellEffectCameraShakes 0x006ECB40): the three ids of a
//      SpellEffectCameraShakes row, each nonzero one added. A kit plays its
//      group (+0x38) once per play (the kit player, 0x0060EDF0); a spell's
//      ground area plays the row its model's $SHK event names, at the
//      event's point (the dynamic object's arm, 0x005D58C0).
//   3. Each frame (the walk 0x00511760): a shake's time is the milliseconds
//      since its spawn times 0.001 plus its pre-roll; at or past its duration
//      it goes. A live one's amplitude is cut by its distance from the eye
//      (0x005116E0: whole within 9 yards, nothing past 80, times
//      0.7^((d - 9) / 9) between). Per axis the strongest wins, the older on
//      a tie; it adds `a = A sin(2 pi t f)`, times exp(-t k) for type 1
//      (0x00511610): axis 0 along the heading, axis 1 across it, axis 2 up.
//   4. The sum moves the eye in world space; it never turns the view (the
//      driver 0x0050E8D0 adds it to the eye at 0x0050EDCC).
//
// The scene's clock times them, so a paused scene holds its shakes. The
// heading is the scene camera's: the world's is the followed unit's facing,
// which its camera looks along, and a scene follows no unit.
//
// The position of a kit's shake: the client plays it in the first effect
// node's arm pass from that node's position, which a bone-attached node has
// not filled yet (+0x48 read at 0x00620E0E, filled at 0x0062101D), so the
// group of a kit whose first effect hangs on a bone plays at the world
// origin, and the camera drops it past 80 yards (benilla spell/shakes "Kit
// shake position fault"). Thunder Clap's cast kit (356) hangs its first
// effect on a hand. Here a kit shakes at the actor, where its node is.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Model::Scene::Shakes {

namespace {

struct Shake {
    int32_t type = 0;
    int32_t axis = 0;
    float amplitude = 0.0f; // yards
    float frequency = 0.0f;
    float duration = 0.0f;
    float preroll = 0.0f;
    float decay = 0.0f;
    float position[3] = {};
    uint32_t spawnMs = 0; // the scene clock
};

std::unordered_map<void *, std::vector<Shake>> g_shakes; // by scene frame, oldest first

const Game::Live<float> kAmplitudeScale{Offsets::VAR_ONE_THIRTY_SIXTH};
const Game::Live<float> kTwoPi{Offsets::VAR_HEADING_TWO_PI};
const Game::Live<float> kHalfPi{Offsets::VAR_HALF_PI};
const Game::Live<float> kNear{Offsets::VAR_CAMERA_SHAKE_NEAR};
const Game::Live<float> kNearSq{Offsets::VAR_CAMERA_SHAKE_NEAR_SQ};
const Game::Live<float> kFarSq{Offsets::VAR_CAMERA_SHAKE_FAR_SQ};
const Game::Live<double> kFalloffBase{Offsets::VAR_CAMERA_SHAKE_FALLOFF_BASE};

// The falloff (0x005116E0): false past the far range; else the amplitude,
// cut beyond the near range. The squared distance is a float, as stored.
bool Falloff(const float (&eye)[3], const Shake &shake, float &amplitude) {
    const float dx = eye[0] - shake.position[0];
    const float dy = eye[1] - shake.position[1];
    const float dz = eye[2] - shake.position[2];
    const float distanceSq = dx * dx + dy * dy + dz * dz;
    if (distanceSq > kFarSq)
        return false;
    amplitude = shake.amplitude;
    if (distanceSq > kNearSq) {
        const float distance = std::sqrt(distanceSq);
        amplitude = static_cast<float>(std::pow(static_cast<double>(kFalloffBase),
                                                static_cast<double>((distance - kNear) / kNear)) *
                                       amplitude);
    }
    return true;
}

// One shake's offset (0x00511610), each step rounded to a float as stored.
void Evaluate(const Shake &shake, float amplitude, float t, float heading, float (&out)[3]) {
    float a = static_cast<float>(amplitude * std::sin(static_cast<float>(kTwoPi * t * shake.frequency)));
    if (shake.type == Offsets::CAMERA_SHAKE_TYPE_DECAYING)
        a = static_cast<float>(a * std::exp(-(t * shake.decay)));
    switch (shake.axis) {
    case 0:
        out[0] += static_cast<float>(a * std::cos(heading));
        out[1] += static_cast<float>(a * std::sin(heading));
        break;
    case 1: {
        const float across = heading + kHalfPi;
        out[0] += static_cast<float>(a * std::cos(across));
        out[1] += static_cast<float>(a * std::sin(across));
        break;
    }
    case 2:
        out[2] += a;
        break;
    default:
        break;
    }
}

} // namespace

void Add(void *frame, int32_t shakeID, const float (&position)[3]) {
    const uint8_t *row = shakeID > 0 ? DBC::Record(Offsets::VAR_CAMERASHAKES_RECORDS, Offsets::VAR_CAMERASHAKES_COUNT,
                                                   static_cast<uint32_t>(shakeID))
                                     : nullptr;
    if (row == nullptr)
        return;
    Shake shake;
    shake.type = Game::Read<int32_t>(row, Offsets::OFF_CAMERASHAKES_TYPE);
    shake.axis = Game::Read<int32_t>(row, Offsets::OFF_CAMERASHAKES_AXIS);
    shake.amplitude = Game::Read<float>(row, Offsets::OFF_CAMERASHAKES_AMPLITUDE) * kAmplitudeScale;
    shake.frequency = Game::Read<float>(row, Offsets::OFF_CAMERASHAKES_FREQUENCY);
    shake.duration = Game::Read<float>(row, Offsets::OFF_CAMERASHAKES_DURATION);
    shake.preroll = Game::Read<float>(row, Offsets::OFF_CAMERASHAKES_PREROLL);
    shake.decay = Game::Read<float>(row, Offsets::OFF_CAMERASHAKES_DECAY);
    for (int i = 0; i < 3; ++i)
        shake.position[i] = position[i];
    shake.spawnMs = ClockMs(frame);
    g_shakes[frame].push_back(shake);
}

void AddGroup(void *frame, int32_t groupID, const float (&position)[3]) {
    const uint8_t *group = groupID > 0 ? DBC::Record(Offsets::VAR_SPELLEFFECTCAMERASHAKES_RECORDS,
                                                     Offsets::VAR_SPELLEFFECTCAMERASHAKES_COUNT,
                                                     static_cast<uint32_t>(groupID))
                                       : nullptr;
    if (group == nullptr)
        return;
    for (int i = 0; i < Offsets::SPELL_EFFECT_CAMERA_SHAKE_COUNT; ++i)
        Add(frame, Game::Read<int32_t>(group, Offsets::OFF_SPELLEFFECTCAMERASHAKES_SHAKES + i * 4), position);
}

void PlayKit(const Actor &actor, const uint8_t *kit) {
    if (kit != nullptr)
        AddGroup(actor.SceneFrame(), Game::Read<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_SHAKE), actor.position);
}

void Offset(void *frame, const float (&eye)[3], float heading, float (&out)[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    const auto it = g_shakes.find(frame);
    if (it == g_shakes.end())
        return;
    std::vector<Shake> &shakes = it->second;
    const uint32_t now = ClockMs(frame);
    // A shake's time: the scene milliseconds since its spawn (an unsigned
    // difference, as the walk forms it) times 0.001, plus its pre-roll.
    auto timeOf = [now](const Shake &shake) {
        return static_cast<float>(now - shake.spawnMs) * 0.001f + shake.preroll;
    };
    shakes.erase(std::remove_if(shakes.begin(), shakes.end(),
                                [&](const Shake &shake) { return !(timeOf(shake) < shake.duration); }),
                 shakes.end());
    if (shakes.empty()) {
        g_shakes.erase(it);
        return;
    }
    constexpr int kAxes = 3;
    int best[kAxes] = {-1, -1, -1};
    float bestAmplitude[kAxes] = {};
    for (int i = 0; i < static_cast<int>(shakes.size()); ++i) {
        const Shake &shake = shakes[static_cast<size_t>(i)];
        float amplitude = 0.0f;
        if (shake.axis < 0 || shake.axis >= kAxes || !Falloff(eye, shake, amplitude))
            continue;
        if (best[shake.axis] < 0 || amplitude > bestAmplitude[shake.axis]) { // the older on a tie
            best[shake.axis] = i;
            bestAmplitude[shake.axis] = amplitude;
        }
    }
    for (int axis = 0; axis < kAxes; ++axis) {
        if (best[axis] >= 0) {
            const Shake &shake = shakes[static_cast<size_t>(best[axis])];
            Evaluate(shake, bestAmplitude[axis], timeOf(shake), heading, out);
        }
    }
}

void Destroy(void *frame) { g_shakes.erase(frame); }

} // namespace Model::Scene::Shakes
