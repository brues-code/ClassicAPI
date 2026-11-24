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

// The hooks that run ActionCam inside 1.12's follow camera update
// (docs/notes/actioncam-3802.md, Part B §1), in 3.80.2's order
// (CGCamera_UpdateFollow 0x141b5e900).
//
// The driver CCamera::Update (0x0050E8D0) rebuilds the eye (+0x08) and the
// basis (+0x14) from scratch every frame and reads neither back, and every
// consumer (render, cull, picking, sound, projection) reads them only after
// OnFrame 0x00511BC0 returns. So a term applied inside the driver reaches the
// whole frame and does not build up from frame to frame. Nobody else hooks
// the driver, the solver or the pivot test (UnitXP_SP3 post-hooks OnFrame,
// above all three).
//
//   - SolveLimits (0x0050E570, the driver's only call to it, after the view
//     angles): before it, the dynamic-pitch blend (3.80.2 step 9) and the
//     over-shoulder offset (step 10); after it, the over-shoulder sweeps
//     (3.80.2's solver) and the solved pivot height for the pitch delta.
//   - Update, after: the view-only pitch delta (step 15) and the
//     over-shoulder self fade (step 19).
//   - PivotBiasAllowed: 3.80.2's GetSmartPivotFlags turns the smart pivot
//     off while dynamic pitch is on within the cutoff distance.

#include "Game.h"
#include "Offsets.h"
#include "camera/DynamicPitch.h"
#include "camera/OverShoulder.h"
#include "time/Clock.h"

#include <cstdint>

namespace Camera::Follow {

namespace {

// This driver call: its camera, target and frame time (from the driver's own
// `now`, OsGetAsyncTimeMs).
struct Frame {
    void *camera = nullptr;
    void *target = nullptr;
    float dt = 0.0f;
};

Frame g_frame;
bool g_hasLastNow = false;
uint32_t g_lastNow = 0;

// What this frame's solve saw: the feet it started from and the height it
// gave.
float g_feet[3] = {0.0f, 0.0f, 0.0f};
float g_solvedHeight = 0.0f;

using Update_t = void(__fastcall *)(void *camera, void *edx, void *target, uint32_t now);
Update_t Update_o = nullptr;

void __fastcall Update_h(void *camera, void *edx, void *target, uint32_t now) {
    g_frame.camera = camera;
    g_frame.target = target;
    g_frame.dt =
        g_hasLastNow ? static_cast<float>(Time::Clock::Elapsed(g_lastNow, now)) * 0.001f : 0.0f;
    g_hasLastNow = true;
    g_lastNow = now;
    Update_o(camera, edx, target, now);
    // The driver returns before building anything for a null target and
    // while the pivot heights are not built; the basis is then last frame's.
    if (target == nullptr || (Game::Read<uint32_t>(camera, Offsets::OFF_CAMERA_MODE) &
                              Offsets::CAMERA_MODE_PIVOT_HEIGHTS) == 0)
        return;
    DynamicPitch::Apply(camera, target, g_solvedHeight, g_frame.dt);
    OverShoulder::Fade(camera, g_feet);
}

const Game::HookAutoRegister _updateHook{Offsets::FUN_CAMERA_UPDATE,
                                         reinterpret_cast<void *>(&Update_h),
                                         reinterpret_cast<void **>(&Update_o)};

using SolveLimits_t = uint32_t(__fastcall *)(void *camera, void *edx, float *pivot,
                                             float *distance, float *height, float *offset,
                                             float band);
SolveLimits_t SolveLimits_o = nullptr;

uint32_t __fastcall SolveLimits_h(void *camera, void *edx, float *pivot, float *distance,
                                  float *height, float *offset, float band) {
    void *target = g_frame.camera == camera ? g_frame.target : nullptr;
    DynamicPitch::UpdateBlend(camera, target, g_frame.dt);
    OverShoulder::Update(camera, target, g_frame.dt);
    // The distance 1.12's solve starts from: the larger of the target and
    // live distances (0x0050E57A).
    const float desired = Game::Read<float>(camera, Offsets::OFF_CAMERA_TARGET_DISTANCE);
    const float live = Game::Read<float>(camera, Offsets::OFF_CAMERA_DISTANCE);
    const float distanceIn = desired > live ? desired : live;
    g_feet[0] = pivot[0];
    g_feet[1] = pivot[1];
    g_feet[2] = pivot[2];

    uint32_t bits = SolveLimits_o(camera, edx, pivot, distance, height, offset, band);
    g_solvedHeight = *height;
    bits |= OverShoulder::Solve(camera, pivot, distance, height, offset, distanceIn);
    return bits;
}

const Game::HookAutoRegister _solveHook{Offsets::FUN_CAMERA_SOLVE_LIMITS,
                                        reinterpret_cast<void *>(&SolveLimits_h),
                                        reinterpret_cast<void **>(&SolveLimits_o)};

using PivotBiasAllowed_t = uint32_t(__fastcall *)(void *camera, void *edx, void *unit);
PivotBiasAllowed_t PivotBiasAllowed_o = nullptr;

uint32_t __fastcall PivotBiasAllowed_h(void *camera, void *edx, void *unit) {
    if (DynamicPitch::DisablesSmartPivot(camera))
        return 0;
    return PivotBiasAllowed_o(camera, edx, unit);
}

const Game::HookAutoRegister _pivotHook{Offsets::FUN_CAMERA_PIVOT_BIAS_ALLOWED,
                                        reinterpret_cast<void *>(&PivotBiasAllowed_h),
                                        reinterpret_cast<void **>(&PivotBiasAllowed_o)};

} // namespace

} // namespace Camera::Follow
