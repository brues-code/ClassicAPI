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

// Over-shoulder, ported from the 3.80.2 Classic client (WowClassicGM-China.exe
// 3.80.2.69496; docs/notes/actioncam-3802.md A §3.4).
//
// 3.80.2 eases a sideways offset (CGCamera_ApplyOverShoulderOffset
// 0x141bc81b0: toward −(object scale × cvar) along the basis's left row,
// stiffness 30; 0 while you target yourself, a tenth while mounted) and
// hands it to its solver (CGCamera_SolveCollision 0x141b5cbc0), which:
//   1. sweeps from the pivot to the trial eye pivot − forward·distance +
//      offset; a hit scales the distance and the offset alike;
//   2. clips the near box at that trial eye; after any shortening, backs
//      the distance off;
//   3. scales the offset by the new distance over the one it started from;
//   4. adds the part of the offset it lost to the eye offset and sweeps
//      that from the trial eye, extended by the back-off, then takes the
//      back-off off again.
// The follow update then puts the pivot at target + offset + height and the
// eye at pivot − forward·distance + eye offset (0x141b5e900 step 14).
//
// 1.12's solver (SolveLimits 0x0050E570) sweeps the boom without an offset,
// so steps 1–4 run after it, on its result, with the engine's own pieces:
// PlaceEye for the trial eye, Intersect with the solver's trace mask for the
// sweeps, ClipNearBox for the near box, its 1/9 back-off. The pivot pointer
// is the driver's own local, which it raises by the height and places the
// eye from, and the offset pointer is its bob-and-shake offset, which it
// adds to the eye; so moving the pivot and adding to the offset is all the
// driver needs. A hit sets 1.12's boom bit, which, like 3.80.2's 0x20000,
// is one of the bits the smart pivot test returns.
//
// 1.12 differences:
//   - 3.80.2 sweeps its whole eye offset every frame; here only while a
//     shoulder or head-movement offset is in play, so the bob and shakes
//     keep 1.12's behaviour otherwise.
//   - 3.80.2's second near-box clip runs along the eye-offset sweep
//     (FUN_141b5ca10, a segment clip); 1.12 has only the trial-eye clip, so
//     that sweep has no clip.
//   - The self fade keeps 1.12's threshold (1.8315) and smoothstep, with
//     3.80.2's over-shoulder changes: the distance is from the eye to the
//     character, not along the boom, and the threshold is
//     threshold·2.5·s + 0.1·(1 − s) with s = clamp((pitch + 60°)·−1/28°,
//     0, 1) (0x141b603d8..0x141b60443).
//   - The view-mode test (1 <= +0xcc <= 5) is far sight in 1.12, as for
//     dynamic pitch.

#include "camera/OverShoulder.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/Spring.h"
#include "camera/View.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cmath>

namespace Camera::OverShoulder {

namespace {

constexpr float kOmega = 30.0f;           // 0x41f00000 at 0x141bc8300
constexpr float kMountedShare = 0.1f;     // 0x141bc82d9
constexpr float kMinSweep = 0.002f;       // 3.80.2's boom guard
constexpr float kMinOffsetSq = 1.0000001e-06f;
constexpr float kMinExtent = 0.001f;
constexpr float kFadeHidden = 1.0f / 360.0f; // 0x00808ABC, 1.12's hidden distance
constexpr float kFadePitch = -1.0471976f;    // 0x14415c8f4, -60 degrees
constexpr float kFadePitchScale = -2.046278f; // 0x14415c94c, -1/28 degrees
constexpr float kFadeFar = 2.5f;             // 0x14415c02c
constexpr float kFadeNear = 0.1f;            // 0x14415b8c4
constexpr float kPi = 3.14159265f;

// 3.80.2's +0x3f0 (and its velocity), for the one world camera; and this
// frame's offset in world space.
struct State {
    const void *camera = nullptr;
    Spring current = {0.0f, 0.0f};
    float offset[3] = {0.0f, 0.0f, 0.0f};
};

State g_state;

State &For(const void *camera) {
    if (g_state.camera != camera) {
        g_state = State{};
        g_state.camera = camera;
    }
    return g_state;
}

uint32_t TraceMask() {
    const void *cvar = Game::Read<void *>(Offsets::VAR_CVAR_CAMERA_WATER_COLLISION);
    const bool water = cvar != nullptr && Game::Read<int32_t>(cvar, Offsets::OFF_CVAR_VALUE_INT) != 0;
    return Offsets::CAMERA_TRACE_MASK | (water ? Offsets::CAMERA_TRACE_MASK_WATER : 0u);
}

void PlaceEye(void *camera, float out[3], const float pivot[3], float distance,
              const float offset[3]) {
    using PlaceEye_t = float *(__thiscall *)(void *, float *, const float *, float, const float *);
    reinterpret_cast<PlaceEye_t>(Offsets::FUN_CAMERA_PLACE_EYE)(camera, out, pivot, distance,
                                                                offset);
}

// Intersect with the hit fraction; false (t untouched) for no hit.
bool Sweep(const float start[3], const float end[3], uint32_t mask, float &t) {
    using Intersect_t = char(__fastcall *)(const float *, const float *, int, float *, float *,
                                           uint32_t);
    float hit[3];
    t = 1.0f;
    return reinterpret_cast<Intersect_t>(Offsets::FUN_CWORLD_INTERSECT)(start, end, 0, hit, &t,
                                                                         mask) != 0;
}

bool ClipNearBox(void *camera, float *distance, const float trial[3], uint32_t mask) {
    using Clip_t = int(__thiscall *)(void *, float *, const float *, uint32_t);
    return reinterpret_cast<Clip_t>(Offsets::FUN_CAMERA_CLIP_NEAR_BOX)(camera, distance, trial,
                                                                        mask) != 0;
}

void Scale(float v[3], float k) {
    v[0] *= k;
    v[1] *= k;
    v[2] *= k;
}

} // namespace

bool IsActive(const void *camera) {
    if (Settings::KeepCharacterCentered() || Settings::ReduceUnexpectedMovement() ||
        Settings::OverShoulder() == 0.0f)
        return false;
    return std::fabs(View::TargetDistance(camera)) >= View::kEpsilon && !View::FarSight(camera);
}

void Update(void *camera, const void *target, float dt) {
    State &s = For(camera);
    s.offset[0] = s.offset[1] = s.offset[2] = 0.0f;
    const bool active = IsActive(camera);
    if (!active && std::fabs(s.current.value) < View::kEpsilon)
        return;
    if (target == nullptr)
        return;
    float shoulder = active ? Settings::OverShoulder() : 0.0f;
    if (shoulder > 0.0f) {
        const uint64_t player = Unit::Identity::PlayerGuid();
        if (Object::Guid(target) == player &&
            Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO) == player) {
            shoulder = 0.0f;
        } else {
            const void *unit = View::Unit(target);
            if (unit != nullptr && View::Mounted(unit))
                shoulder *= kMountedShare;
        }
    }
    if (std::fabs(shoulder) >= View::kEpsilon || std::fabs(s.current.value) >= View::kEpsilon) {
        const uint8_t *data = Object::Data(target);
        const float scale =
            data != nullptr ? Game::Read<float>(data, Offsets::OFF_OBJECT_FIELD_SCALE_X) : 0.0f;
        SmoothCriticallyDamped(s.current, -(scale * shoulder), kOmega, dt);
    }
    float left[3];
    View::Row(camera, Offsets::VTBL_CAMERA_LEFT, left);
    for (int i = 0; i < 3; ++i)
        s.offset[i] = s.current.value * left[i];
}

uint32_t Solve(void *camera, float *pivot, float *distance, const float *height, float *offset,
               float distanceIn, bool sweepEyeOffset) {
    State &s = For(camera);
    const float *start = s.offset;
    const bool hasShoulder = start[0] != 0.0f || start[1] != 0.0f || start[2] != 0.0f;
    if (!hasShoulder && !sweepEyeOffset)
        return 0;
    const uint32_t mask = TraceMask();
    const float backoff = Game::Read<float>(Offsets::VAR_CAMERA_COLLISION_BACKOFF);
    const float top[3] = {pivot[0], pivot[1], pivot[2] + *height};
    float shoulder[3] = {start[0], start[1], start[2]};
    float d = *distance;
    float trial[3];
    uint32_t bits = 0;

    // Steps 1 and 2.
    if (hasShoulder && d > kMinSweep) {
        bool hit = false;
        PlaceEye(camera, trial, top, d, shoulder);
        float t;
        if (Sweep(top, trial, mask, t)) {
            d *= t;
            Scale(shoulder, t);
            hit = true;
        }
        if (d >= kMinSweep) {
            if (hit)
                PlaceEye(camera, trial, top, d, shoulder);
            if (ClipNearBox(camera, &d, trial, mask))
                hit = true;
            if (std::fabs(*distance - d) >= View::kEpsilon) {
                d -= backoff;
                if (d <= 0.0f)
                    d = 0.0f;
            }
        }
        if (hit)
            bits |= Offsets::CAMERA_MODE_BOOM_HIT;
    }

    // Step 3.
    if (hasShoulder && std::fabs(distanceIn - d) >= View::kEpsilon &&
        std::fabs(d) >= View::kEpsilon)
        Scale(shoulder, d / distanceIn);

    // Step 4.
    for (int i = 0; i < 3; ++i)
        offset[i] += start[i] - shoulder[i];
    const float lengthSq = offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
    if (kMinOffsetSq < lengthSq) {
        PlaceEye(camera, trial, top, d, shoulder);
        const float length = std::sqrt(lengthSq);
        float extent = length + backoff;
        float v[3] = {offset[0], offset[1], offset[2]};
        Scale(v, extent / length);
        const float end[3] = {trial[0] + v[0], trial[1] + v[1], trial[2] + v[2]};
        float t;
        if (Sweep(trial, end, mask, t)) {
            Scale(v, t);
            extent *= t;
        }
        if (extent <= kMinExtent) {
            offset[0] = offset[1] = offset[2] = 0.0f;
        } else {
            const float keep = extent - backoff;
            Scale(v, (keep <= 0.0f ? 0.0f : keep) / extent);
            offset[0] = v[0];
            offset[1] = v[1];
            offset[2] = v[2];
        }
    }

    *distance = d;
    for (int i = 0; i < 3; ++i)
        pivot[i] += shoulder[i];
    return bits;
}

void Fade(void *camera, const float feet[3]) {
    if (!IsActive(camera))
        return;
    const float *eye = Game::Ptr<float>(camera, Offsets::OFF_CAMERA_POSITION);
    const float dx = eye[0] - feet[0];
    const float dy = eye[1] - feet[1];
    const float dz = eye[2] - feet[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz) -
                    Game::Read<float>(camera, Offsets::OFF_CAMERA_NEAR_CLIP);
    float s = (Game::Read<float>(camera, Offsets::OFF_CAMERA_PITCH) - kFadePitch) * kFadePitchScale;
    s = s < 0.0f ? 0.0f : (s < 1.0f ? s : 1.0f);
    const float threshold = Game::Read<float>(Offsets::VAR_CAMERA_NEAR_DISTANCE) * kFadeFar * s +
                            kFadeNear * (1.0f - s);
    uint32_t alpha = 255;
    if (d < threshold) {
        if (d <= kFadeHidden)
            alpha = 0;
        else
            alpha = static_cast<uint32_t>((1.0f - std::cos(kPi * d / threshold)) * 0.5f * 255.0f);
    }
    using SetTargetAlpha_t = void(__thiscall *)(void *, uint32_t);
    reinterpret_cast<SetTargetAlpha_t>(Offsets::FUN_CAMERA_SET_TARGET_ALPHA)(camera, alpha);
}

} // namespace Camera::OverShoulder
