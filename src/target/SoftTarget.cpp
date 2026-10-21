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

// The soft-target scan, from 1.12's own pieces:
//
//   - "On screen" is the tab-target collector's IsEntityDrawn on the
//     object's world node.
//   - The arc (SoftTarget<Kind>Arc: 0 = directly in front, 1 = in the front
//     yaw arc, 2 = anywhere in the targeting area): 1 is the tab collector's
//     front cone (pi/6 either side, from the engine's own floats); 0 is a
//     strip along the forward line, measured in Classic Era 1.15.9 (below).
//     "Front" is the camera's, not the character's: in Classic Era 1.15 a
//     character turned away still soft targets what the camera faces. The
//     arc is centred on the player, turned to the camera's yaw (CGCamera::
//     Forward, the vector 4.3.4's GetCameraFacing reads).
//   - The range is SoftTarget<Kind>Range, capped by the tab-target range
//     targetNearestDistance ("limited to tab targeting ... ranges").
//   - The closest candidate wins — Classic Era 1.15 probes for interact,
//     enemy and friend alike, not the one nearest the forward line.
//
// Scan only on the UI frame tick or from Lua, never on the world tick:
// there every world node reads as culled (probe: 3 drawn from Lua, 0 on the
// world tick), while the UI pass — where the engine's own hover
// classification runs, CGWorldFrame::OnUpdate — and Lua see the drawn state.

#include "target/SoftTarget.h"

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cmath>
#include <cstdlib>

namespace Target::SoftTarget {

namespace {

using Forward_t = void(__thiscall *)(void *camera, float *out);
using Heading_t = float(__fastcall *)(const float *from, const float *to);
using IsDrawn_t = uint32_t(__fastcall *)(void *node, void *edx);
using EnumCallback_t = int(__fastcall *)(void *ctx, void *edx, uint64_t guid);
using EnumVisibleObjects_t = int(__fastcall *)(EnumCallback_t cb, void *ctx);

// Arc 0's strip: how far beside the forward line an object may sit, in
// yards. Classic Era 1.15.9 probe, a mailbox, view swept until
// `softinteract` dropped: ON span ~33 deg close in and ~15 deg at ~9 yd,
// both a half-width of ~1.2 yd — a strip, not a cone.
constexpr float kArc0HalfWidth = 1.2f;

float FloatCVar(CVar::Factory::Handle cvar, float fallback) {
    const char *s = cvar ? CVar::Factory::GetString(cvar) : nullptr;
    return s ? static_cast<float>(std::atof(s)) : fallback;
}

// The camera's yaw, in the heading convention FUN_HEADING_BETWEEN_POINTS
// uses (atan2 of the y and x deltas). False before the world frame has a
// camera.
bool CameraYaw(float &yaw) {
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    void *camera =
        worldFrame ? Game::Read<void *>(worldFrame, Offsets::OFF_GAME_STATE_CAMERA_PTR) : nullptr;
    if (camera == nullptr)
        return false;
    float forward[3];
    auto *vtable = Game::Read<uintptr_t *>(camera, 0);
    reinterpret_cast<Forward_t>(vtable[Offsets::VTBL_CAMERA_FORWARD / sizeof(uintptr_t)])(camera,
                                                                                         forward);
    if (forward[0] == 0.0f && forward[1] == 0.0f)
        return false;
    yaw = std::atan2(forward[1], forward[0]);
    return true;
}

bool IsDrawn(const void *object) {
    void *node = Game::Read<void *>(object, Offsets::OFF_CGOBJECT_WORLD_OBJECT);
    return reinterpret_cast<IsDrawn_t>(Offsets::FUN_WORLD_OBJECT_VISIBLE)(node, nullptr) != 0;
}

bool InArc(const Area &a, const float pos[3]) {
    if (a.arc >= 2)
        return true;
    const float pi = Game::Read<float>(Offsets::VAR_TARGETING_PI);
    const float heading =
        reinterpret_cast<Heading_t>(Offsets::FUN_HEADING_BETWEEN_POINTS)(a.pos, pos);
    // The tab collector's form, with the camera's yaw: (heading - yaw + 4 pi) mod 2 pi.
    const float angle = std::fmod(heading - a.yaw + 4.0f * pi, 2.0f * pi);
    if (a.arc == 1)
        return angle < a.coneHalf || angle > 2.0f * pi - a.coneHalf;
    const float dx = pos[0] - a.pos[0];
    const float dy = pos[1] - a.pos[1];
    const float flat = std::sqrt(dx * dx + dy * dy);
    return std::cos(angle) > 0.0f && std::fabs(flat * std::sin(angle)) <= kArc0HalfWidth;
}

// Drawn, and where: the checks before the arc, which needs the position.
bool Locate(const Area &a, void *object, float pos[3], float &distSq) {
    if (object == nullptr || object == a.player || !IsDrawn(object) ||
        !Unit::Position::Read(object, pos))
        return false;
    const float dx = pos[0] - a.pos[0];
    const float dy = pos[1] - a.pos[1];
    const float dz = pos[2] - a.pos[2];
    distSq = dx * dx + dy * dy + dz * dz;
    return true;
}

struct Scan {
    const Area *area;
    int typeMask;
    Accept accept;
    void *ctx;
    uint64_t best;
    float bestSq;
};

int __fastcall ScanCallback(void *ctx, void *, uint64_t guid) {
    auto &s = *static_cast<Scan *>(ctx);
    void *object = Object::ByGuid(s.typeMask, guid, nullptr, 0);
    float pos[3];
    float distSq;
    if (!Locate(*s.area, object, pos, distSq) || distSq >= s.bestSq || !InArc(*s.area, pos) ||
        !s.accept(*s.area, object, distSq, s.ctx))
        return 1;
    s.best = guid;
    s.bestSq = distSq;
    return 1;
}

} // namespace

bool Enabled(CVar::Factory::Handle enable) {
    const int v = CVar::Factory::GetInt(enable);
    return v == 2 || v == 3;
}

bool Begin(Area &area, CVar::Factory::Handle arc, CVar::Factory::Handle range,
           float defaultRange) {
    area = Area{};
    area.player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (area.player == nullptr || !Unit::Position::Read(area.player, area.pos) ||
        !CameraYaw(area.yaw))
        return false;
    area.arc = CVar::Factory::GetInt(arc);
    area.coneHalf = Game::Read<float>(Offsets::VAR_TARGETING_PI) *
                    Game::Read<float>(Offsets::VAR_TARGETING_ONE_SIXTH);
    float r = FloatCVar(range, defaultRange);
    if (const void *tabRange = Game::Read<void *>(Offsets::VAR_CVAR_TARGET_NEAREST_DISTANCE)) {
        const float cap = Game::Read<float>(tabRange, Offsets::OFF_CVAR_VALUE_FLOAT);
        if (cap < r)
            r = cap;
    }
    area.rangeSq = r * r;
    return true;
}

bool InView(const Area &area, void *object, float &distSq) {
    float pos[3];
    return Locate(area, object, pos, distSq) && InArc(area, pos);
}

uint64_t Nearest(const Area &area, int typeMask, Accept accept, void *ctx) {
    Scan s{&area, typeMask, accept, ctx, 0, HUGE_VALF};
    reinterpret_cast<EnumVisibleObjects_t>(Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS)(
        &ScanCallback, &s);
    return s.best;
}

} // namespace Target::SoftTarget
