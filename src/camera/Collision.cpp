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

// The rest of 3.80.2's camera solver (WowClassicGM-China.exe 3.80.2.69496,
// CGCamera_SolveCollision 0x141b5cbc0; docs/notes/actioncam-3802.md A §3.4,
// §3.7), run on 1.12's result. After its boom, 3.80.2:
//   1. sweeps from the pivot to the trial eye pivot − forward·distance +
//      shoulder offset; a hit scales the distance and the offset alike;
//   2. clips the near box at that trial eye; after any shortening, backs
//      the distance off;
//   3. with cameraIndirectVisibility, after a boom hit, looks for the
//      camera around the obstacle (CGCamera_ProbeIndirectVisibility
//      0x141b5e280, below);
//   4. scales the offset by the new distance over the one it started from;
//   5. adds the part of the offset it lost to the eye offset and sweeps
//      that from the trial eye, extended by the back-off, then takes the
//      back-off off again.
// The follow update then puts the pivot at target + offset + height and the
// eye at pivot − forward·distance + eye offset (0x141b5e900 step 14).
//
// 1.12's solver (SolveLimits 0x0050E570) sweeps the boom without an offset,
// so these run after it, with the engine's own pieces: PlaceEye for the
// trial eye, Intersect with the solver's trace mask for the sweeps,
// ClipNearBox for the near box, its 1/9 back-off. The pivot pointer is the
// driver's own local, which it raises by the height and places the eye from,
// and the offset pointer is its bob-and-shake offset, which it adds to the
// eye; so moving the pivot and adding to the offset is all the driver needs.
// A hit sets 1.12's boom bit, which, like 3.80.2's 0x20000, is one of the
// bits the smart pivot test returns.
//
// Indirect visibility (0x141b5e280 with its probe FUN_141b5d8e0). When the
// live distance is at least a yard past the solved one, four probes start
// beside the character: above it (cameraIndirectOffset along the up row,
// and at least 2.5 yards up; not under a ceiling), left and right of it
// (cameraIndirectOffset + object scale along the left row), and low (5/6
// yard up, only for a pivot above that). Each sweeps from the pivot to its
// start (pulled back 0.2 yard short of a hit) and from there to the trial
// eye at the live distance (0.2 yard past it). The first probe with both
// legs clear whose distance (trial eye to pivot) is over a yard past the
// solved one sets the distance and clears the boom bit.
//
// 1.12 differences:
//   - 3.80.2 sweeps its whole eye offset every frame; here only while a
//     shoulder or head-movement offset is in play, so the bob and shakes
//     keep 1.12's behaviour otherwise.
//   - 3.80.2 also clips its near box along each probe leg and along the
//     eye-offset sweep (FUN_141b5ca10, a segment clip); 1.12 has only the
//     trial-eye clip, so those run as sweeps alone.
//   - A blocked probe keeps going in 3.80.2 (two tries along the boom), but
//     its distance only reaches the result through the best-of a clear
//     probe takes, which the clear probe's full distance bounds; so a
//     blocked probe counts as nothing here.
//   - "Under a ceiling" is 1.12's vertical-probe bit (0x20000; 3.80.2's
//     +0xaa bit 0, its 0x10000).

#include "camera/Collision.h"

#include "Game.h"
#include "Offsets.h"
#include "camera/Settings.h"
#include "camera/View.h"
#include "object/Resolve.h"

#include <cmath>

namespace Camera::Collision {

namespace {

constexpr float kMinSweep = 0.002f;       // 3.80.2's boom guard
constexpr float kMinOffsetSq = 1.0000001e-06f;
constexpr float kMinExtent = 0.001f;
constexpr uint32_t kCeilingBit = 0x20000; // 1.12's vertical-probe hit
constexpr float kProbeMargin = 0.2f;      // 0x3e4ccd0d
constexpr float kProbeMinUp = 2.5f;       // 0x40200000
constexpr float kProbeLow = 0.8333333f;
constexpr float kProbeGain = 1.0f;        // a probe must beat the solve by a yard

uint32_t TraceMask() {
    const void *cvar = Game::Read<void *>(Offsets::VAR_CVAR_CAMERA_WATER_COLLISION);
    const bool water =
        cvar != nullptr && Game::Read<int32_t>(cvar, Offsets::OFF_CVAR_VALUE_INT) != 0;
    return Offsets::CAMERA_TRACE_MASK | (water ? Offsets::LIQUID_TRIANGLE_CLASS : 0u);
}

void PlaceEye(void *camera, float out[3], const float pivot[3], float distance,
              const float offset[3]) {
    using PlaceEye_t = float *(__thiscall *)(void *, float *, const float *, float, const float *);
    reinterpret_cast<PlaceEye_t>(Offsets::FUN_CAMERA_PLACE_EYE)(camera, out, pivot, distance,
                                                                offset);
}

// Intersect with the hit fraction in 0..1; false for no hit. A fraction
// past the end counts as clear (FUN_CWORLD_INTERSECT's contract), and one
// before the start, which UnitXP_SP3's 0.01 shortening of camera-mask hits
// can give, is a hit at the start.
bool Sweep(const float start[3], const float end[3], uint32_t mask, float &t) {
    using Intersect_t = char(__fastcall *)(const float *, const float *, int, float *, float *,
                                           uint32_t);
    float hit[3];
    t = 1.0f;
    if (reinterpret_cast<Intersect_t>(Offsets::FUN_CWORLD_INTERSECT)(start, end, 0, hit, &t,
                                                                      mask) == 0 ||
        t > 1.0f)
        return false;
    if (t < 0.0f)
        t = 0.0f;
    return true;
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

float Length(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// FUN_141b5d8e0 for one start point: true when both legs are clear, with
// the trial eye's distance from the pivot in `distance`.
bool Probe(const float top[3], float start[3], const float eye[3], uint32_t mask,
           float &distance) {
    float v[3] = {start[0] - top[0], start[1] - top[1], start[2] - top[2]};
    const float length = Length(v);
    if (length < kMinExtent)
        return false;
    Scale(v, 1.0f / length);
    float t;
    if (Sweep(top, start, mask, t)) {
        const float reach = t * length;
        if (reach <= kProbeMargin)
            return false;
        for (int i = 0; i < 3; ++i)
            start[i] = top[i] + v[i] * (reach - kProbeMargin);
    }
    float w[3] = {eye[0] - start[0], eye[1] - start[1], eye[2] - start[2]};
    const float leg = Length(w);
    if (leg < kMinExtent)
        return false;
    Scale(w, 1.0f / leg);
    const float end[3] = {eye[0] + w[0] * kProbeMargin, eye[1] + w[1] * kProbeMargin,
                          eye[2] + w[2] * kProbeMargin};
    if (Sweep(start, end, mask, t))
        return false;
    const float u[3] = {eye[0] - top[0], eye[1] - top[1], eye[2] - top[2]};
    distance = Length(u);
    return true;
}

// CGCamera_ProbeIndirectVisibility 0x141b5e280. True when it set the
// distance.
bool IndirectVisibility(void *camera, const void *target, const float feet[3], float height,
                        const float shoulder[3], uint32_t mask, uint32_t bits, float &d) {
    const float live = Game::Read<float>(camera, Offsets::OFF_CAMERA_DISTANCE);
    if (live - d < kProbeGain)
        return false;
    float left[3], up[3];
    View::Row(camera, Offsets::VTBL_CAMERA_LEFT, left);
    View::Row(camera, Offsets::VTBL_CAMERA_UP, up);
    const float offset = Settings::IndirectOffset();
    const uint8_t *data = target != nullptr ? Object::Data(target) : nullptr;
    const float scale =
        data != nullptr ? Game::Read<float>(data, Offsets::OFF_OBJECT_FIELD_SCALE_X) : 0.0f;
    const float side = offset + scale;
    const float top[3] = {feet[0], feet[1], feet[2] + height};
    float eye[3];
    PlaceEye(camera, eye, top, live, shoulder);
    for (int i = 0; i < 4; ++i) {
        float start[3];
        if (i == 0) {
            if ((bits & kCeilingBit) != 0)
                continue;
            const float lift = height > kProbeMinUp ? height : kProbeMinUp;
            for (int k = 0; k < 3; ++k)
                start[k] = feet[k] + up[k] * offset;
            start[2] += lift;
        } else if (i == 1 || i == 2) {
            const float sign = i == 1 ? 1.0f : -1.0f;
            for (int k = 0; k < 3; ++k)
                start[k] = feet[k] + sign * left[k] * side;
        } else {
            if (height <= kProbeLow)
                continue;
            start[0] = feet[0];
            start[1] = feet[1];
            start[2] = feet[2] + kProbeLow;
        }
        float distance = 0.0f;
        if (Probe(top, start, eye, mask, distance) && d + kProbeGain < distance) {
            d = distance;
            return true;
        }
    }
    return false;
}

} // namespace

uint32_t Solve(void *camera, const void *target, float *pivot, float *distance,
               const float *height, float *offset, float distanceIn, const float shoulderIn[3],
               bool sweepEyeOffset, uint32_t bits) {
    const bool hasShoulder = shoulderIn[0] != 0.0f || shoulderIn[1] != 0.0f || shoulderIn[2] != 0.0f;
    const bool indirect = Settings::IndirectVisibility();
    if (!hasShoulder && !sweepEyeOffset && !indirect)
        return bits;
    const uint32_t mask = TraceMask();
    const float backoff = Game::Read<float>(Offsets::VAR_CAMERA_COLLISION_BACKOFF);
    const float top[3] = {pivot[0], pivot[1], pivot[2] + *height};
    float shoulder[3] = {shoulderIn[0], shoulderIn[1], shoulderIn[2]};
    float d = *distance;
    float trial[3];

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
    if (indirect && (bits & Offsets::CAMERA_MODE_BOOM_HIT) != 0 &&
        IndirectVisibility(camera, target, pivot, *height, shoulder, mask, bits, d))
        bits &= ~static_cast<uint32_t>(Offsets::CAMERA_MODE_BOOM_HIT);

    // Step 4.
    if (hasShoulder && std::fabs(distanceIn - d) >= View::kEpsilon &&
        std::fabs(d) >= View::kEpsilon)
        Scale(shoulder, d / distanceIn);

    // Step 5.
    if (hasShoulder || sweepEyeOffset) {
        for (int i = 0; i < 3; ++i)
            offset[i] += shoulderIn[i] - shoulder[i];
        const float lengthSq =
            offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
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
    }

    *distance = d;
    for (int i = 0; i < 3; ++i)
        pivot[i] += shoulder[i];
    return bits;
}

} // namespace Camera::Collision
