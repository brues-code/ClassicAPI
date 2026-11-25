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
//     angles and with the bob and shakes already in the eye offset): before
//     it, head movement (3.80.2 steps 4 and 11), the vertical stabilizer
//     (inside step 7, raising the pivot by its offset), the dynamic-pitch
//     blend (step 9) and the over-shoulder offset (step 10); after it, the
//     rest of 3.80.2's solver (camera/Collision.cpp: over-shoulder sweeps,
//     indirect visibility, eye-offset sweep) and the solved pivot height for
//     the pitch delta.
//   - UpdateTransitions (0x0050F160), after: the camera follow (step 8,
//     camera/AutoFollow.cpp).
//   - Update, after: the view-only pitch delta (step 15), target focus
//     (step 16) and the over-shoulder self fade (step 19); then the camera
//     follow's last-frame record.
//   - PivotBiasAllowed: 3.80.2's GetSmartPivotFlags turns the smart pivot
//     off while dynamic pitch is on within the cutoff distance.

#include "Game.h"
#include "Offsets.h"
#include "camera/AutoFollow.h"
#include "camera/Collision.h"
#include "camera/DynamicPitch.h"
#include "camera/HeadMovement.h"
#include "camera/OverShoulder.h"
#include "camera/Stabilizer.h"
#include "camera/TargetFocus.h"
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

// What this frame's solve saw: the feet it started from, the height it gave
// and the pivot the eye is placed from (feet + shoulder + height, the
// driver's own sum).
float g_feet[3] = {0.0f, 0.0f, 0.0f};
float g_solvedHeight = 0.0f;
float g_pivot[3] = {0.0f, 0.0f, 0.0f};

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
    TargetFocus::Apply(camera, g_feet, g_pivot, g_frame.dt);
    OverShoulder::Fade(camera, g_feet);
    AutoFollow::Record(camera, g_feet);
}

const Game::HookAutoRegister _updateHook{Offsets::FUN_CAMERA_UPDATE,
                                         reinterpret_cast<void *>(&Update_h),
                                         reinterpret_cast<void **>(&Update_o)};

// The transitions (one caller, the driver): 3.80.2's camera follow runs
// right after them, before the view angles (step 8).
using UpdateTransitions_t = void(__fastcall *)(void *camera, void *edx, void *target, uint32_t now);
UpdateTransitions_t UpdateTransitions_o = nullptr;

void __fastcall UpdateTransitions_h(void *camera, void *edx, void *target, uint32_t now) {
    UpdateTransitions_o(camera, edx, target, now);
    AutoFollow::Update(camera, target, now, g_frame.camera == camera ? g_frame.dt : 0.0f);
}

const Game::HookAutoRegister _transitionsHook{Offsets::FUN_CAMERA_UPDATE_TRANSITIONS,
                                              reinterpret_cast<void *>(&UpdateTransitions_h),
                                              reinterpret_cast<void **>(&UpdateTransitions_o)};

using SolveLimits_t = uint32_t(__fastcall *)(void *camera, void *edx, float *pivot,
                                             float *distance, float *height, float *offset,
                                             float band);
SolveLimits_t SolveLimits_o = nullptr;

uint32_t __fastcall SolveLimits_h(void *camera, void *edx, float *pivot, float *distance,
                                  float *height, float *offset, float band) {
    void *target = g_frame.camera == camera ? g_frame.target : nullptr;
    g_feet[0] = pivot[0];
    g_feet[1] = pivot[1];
    g_feet[2] = pivot[2];
    bool tracked = false;
    const bool headOffset = HeadMovement::Apply(camera, target, g_feet,
                                                Stabilizer::Offset(camera), g_frame.dt, offset,
                                                tracked);
    pivot[2] += Stabilizer::Update(camera, target, g_feet, tracked, g_frame.dt);
    DynamicPitch::UpdateBlend(camera, target, g_frame.dt);
    OverShoulder::Update(camera, target, g_frame.dt);
    // The distance 1.12's solve starts from: the larger of the target and
    // live distances (0x0050E57A).
    const float desired = Game::Read<float>(camera, Offsets::OFF_CAMERA_TARGET_DISTANCE);
    const float live = Game::Read<float>(camera, Offsets::OFF_CAMERA_DISTANCE);
    const float distanceIn = desired > live ? desired : live;

    uint32_t bits = SolveLimits_o(camera, edx, pivot, distance, height, offset, band);
    g_solvedHeight = *height;
    float shoulder[3];
    OverShoulder::Offset(camera, shoulder);
    bits = Collision::Solve(camera, target, pivot, distance, height, offset, distanceIn, shoulder,
                            headOffset, bits);
    g_pivot[0] = pivot[0];
    g_pivot[1] = pivot[1];
    g_pivot[2] = pivot[2] + *height;
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
