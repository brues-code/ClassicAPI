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

// The soft-target circles: retail draws a dimmer selection circle under
// `softenemy` and `softfriend` (retail screenshot: the soft enemy's circle in
// its reaction colour, the soft friend's too, both fainter than a target's).
// No cvar of their own: they follow SoftTargetEnemy / SoftTargetFriend and the
// engine's ObjectSelectionCircle.
//
// The engine draws the target's circle at the base per-frame step
// (FUN_CGOBJECT_FRAME_UPDATE, run per entity inside DrawEntityModels, so the
// circle lands before the entity's blob shadow). The soft circles are drawn
// at the same step, by a mirror of the unit's slot 12 (0x608E00) that differs
// only in the radius and the colour's alpha byte. A soft unit that is also the target keeps
// the engine's own circle.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "target/SoftUnit.h"
#include "tick/FrameTick.h"

#include <cstdint>

namespace Target::SoftTargetCircle {

namespace {

using FrameUpdate_t = void(__fastcall *)(void *obj, void *edx, int arg);
using GetPosition_t = float *(__thiscall *)(void *obj, float out[3]);
using SelectionColor_t = void(__thiscall *)(void *obj, uint32_t *argb);
using YawMatrix_t = void(__thiscall *)(void *obj, float m[16], const float point[3]);
using CircleState_t = void(__thiscall *)(void *obj);
using DrawDecal_t = void(__fastcall *)(const float box[6], const uint32_t *argb, const float m[16],
                                       float fade, uint32_t flags);
using GxState_t = void(__cdecl *)();

// The arguments 0x608E00 passes FUN_DRAW_DECAL.
constexpr float kDecalFade = 0.5f;
constexpr uint32_t kDecalFlags = 0x200122;

// The soft circle against the target's, from retail screenshots of one unit
// (Master Wood, same zoom: its title 104 px wide in both) as softfriend and
// as target. Width: 64 px soft, 75 px target, 0.85. Brightness: the ring's
// peak green over the grass median, +116 soft, +207 target (near 255, so a
// floor), at most 0.56; the blend is additive, so the excess follows alpha.
constexpr float kSoftScale = 0.85f;
constexpr uint32_t kSoftAlpha = 0x80;

FrameUpdate_t g_origFrameUpdate = nullptr;

// The soft units (FrameTick), 0 when the side is off.
uint64_t g_soft[2] = {};

template <typename Fn> Fn Slot(void *obj, int offset) {
    return Game::Read<Fn>(Game::Read<void *>(obj, 0), offset);
}

// 0x608E00 with the radius scaled and the colour's alpha replaced.
void DrawCircle(void *unit, float scale, uint32_t alpha) {
    const auto getPosition = Slot<GetPosition_t>(unit, Offsets::OFF_CGOBJECT_VTBL_GET_POSITION);
    float pos[3];
    getPosition(unit, pos);
    const float r =
        scale * Game::Read<float>(unit, Offsets::OFF_CGUNIT_SELECTION_RADIUS) *
        Game::Read<float>(Game::Read<void *>(unit, Offsets::OFF_UNIT_GUID_PTR),
                          Offsets::OFF_OBJECT_FIELD_SCALE_X);
    const float box[6] = {pos[0] - r, pos[1] - r, pos[2] - 2.0f * r,
                          pos[0] + r, pos[1] + r, pos[2] + 2.0f * r};

    uint32_t argb = 0;
    Slot<SelectionColor_t>(unit, Offsets::OFF_CGOBJECT_VTBL_SELECTION_COLOR)(unit, &argb);
    argb = (argb & 0x00FFFFFF) | (alpha << 24);

    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float point[3];
    reinterpret_cast<YawMatrix_t>(Offsets::FUN_BUILD_YAW_MATRIX)(unit, m,
                                                                 getPosition(unit, point));

    reinterpret_cast<GxState_t>(Offsets::FUN_GX_STATE_PUSH)();
    reinterpret_cast<CircleState_t>(Offsets::FUN_SELECTION_CIRCLE_STATE)(unit);
    reinterpret_cast<DrawDecal_t>(Offsets::FUN_DRAW_DECAL)(box, &argb, m, kDecalFade,
                                                           kDecalFlags);
    reinterpret_cast<GxState_t>(Offsets::FUN_GX_STATE_POP)();
}

// The engine's own gate first (0x614AA5-0x614AD4: not the target, the
// cvar on), then a soft unit's circle where the target's would be drawn.
void __fastcall FrameUpdate_h(void *obj, void *edx, int arg) {
    const uint64_t guid =
        Game::Read<uint64_t>(Game::Read<void *>(obj, Offsets::OFF_UNIT_GUID_PTR), 0);
    if (guid != 0 && (guid == g_soft[0] || guid == g_soft[1]) &&
        guid != Game::Read<uint64_t>(Offsets::VAR_CURRENT_SELECTION_GUID_LO)) {
        void *cvar = Game::Read<void *>(Offsets::VAR_CVAR_OBJECT_SELECTION_CIRCLE);
        if (cvar != nullptr && CVar::Factory::GetInt(cvar) != 0)
            DrawCircle(obj, kSoftScale, kSoftAlpha);
    }
    g_origFrameUpdate(obj, edx, arg);
}

const Game::HookAutoRegister _hook{Offsets::FUN_CGOBJECT_FRAME_UPDATE,
                                   reinterpret_cast<void *>(&FrameUpdate_h),
                                   reinterpret_cast<void **>(&g_origFrameUpdate)};

// Picked on the UI frame, after the scene: the scan takes only objects the
// scene drew this frame, so a pick from WorldTick (before the scene) found
// none (probe: softenemy resolved from Lua, the WorldTick cache stayed 0).
// The circles draw the next scene, as the icons do.
void OnFrameTick() {
    g_soft[0] = SoftUnit::Get(SoftUnit::Side::Enemy);
    g_soft[1] = SoftUnit::Get(SoftUnit::Side::Friend);
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

} // namespace

} // namespace Target::SoftTargetCircle
