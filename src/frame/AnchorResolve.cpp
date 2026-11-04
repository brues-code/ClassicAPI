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

// A script that re-anchors a region while that region is being laid out no
// longer frees the anchor point the layout is reading.
//
// An edge resolver walks the region's points (FUN_LAYOUT_POINTS_X / Y) and asks
// each for its coordinate (VTBL_ANCHOR_NODE_X / Y). When the point's relative
// frame is deferred, the getter first ends the deferral with
// FUN_REGION_LAYOUT_REALIZE(0), which lays the relative out at once: its
// OnSizeChanged runs right there. The getter then reads the point again. If
// that script moved the region with SetPoint or ClearAllPoints, the point is
// freed, and the read gets heap memory: an access violation in
// FUN_REGION_GET_RECT on the "relative frame" the freed point holds (crash
// log 2026-11-03: `[ecx+0x3c]`, ecx = 5). The path is easy to reach: Show runs
// a frame's OnSizeChanged before its children leave their deferral, and the
// ported ScrollBox reads sizes from that handler.
//
// 3.3.5 fixed it in CFramePoint::GetRelativeRect (0x0049C830): the point is
// marked busy around its relative's resolve, a point changed while busy is
// flagged, and the getter stops on the flag without reading the point again.
// GetFramePointX / Y then return a failure, and GetHorizontalFramePoint /
// GetVerticalFramePoint (0x00488ED0 / 0x00488F10) walk the points again, up to
// ten times, with the anchors as they are now. 1.12 has no flag word in its
// point, and SetPoint never changes a point in place (it frees the old one and
// allocates a new one), so "changed" here means "freed": the point's deleting
// destructor (VTBL_ANCHOR_NODE_DELETE, every free goes through it) flags a busy
// point.
//
// The getters end the deferral themselves, as 3.3.5 does, and stop when the
// point is flagged. Otherwise they call the engine's own getter for the value.
// If the resolve deferred the relative again (a Hide in its script), the
// getter would end the deferral a second time. 3.3.5 ends it once and then
// reads the rect, so the bit is hidden from the engine's getter during the
// read. The walks are the engine's own, retried as 3.3.5 retries them.
//
// Nothing is hooked: the point vtable (VAR_ANCHOR_VTABLE_WITH_RELATIVE) is the
// only way to reach the getters and the destructor, and the six edge
// resolvers are the only callers of the walks.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::AnchorResolve {

namespace {

using Coordinate_t = float(__thiscall *)(void *point, float scale);
using Walk_t = float(__thiscall *)(void *layout, const int *points, int count);
using Delete_t = void *(__thiscall *)(void *point, int flags);
using Realize_t = void(__thiscall *)(void *layout, int defer);

// 3.3.5 GetHorizontalFramePoint / GetVerticalFramePoint: `for (i = 0; i < 10; i++)`.
constexpr int kWalkAttempts = 10;

// A point whose relative is being laid out. Its destructor sets `freed`.
struct BusyPoint {
    void *point;
    bool freed;
    BusyPoint *outer;
};

// A point walk in progress. A getter sets `changed` when its point was freed.
struct PointWalk {
    bool changed;
    PointWalk *outer;
};

BusyPoint *g_busy = nullptr;
PointWalk *g_walk = nullptr;

float Undefined() { return Game::Read<float>(Offsets::VAR_LAYOUT_UNDEFINED); }

float Coordinate(uintptr_t engine, void *point, float scale) {
    if (g_walk != nullptr && g_walk->changed)
        return Undefined(); // 3.3.5 stops the walk at the changed point

    auto *relative = Game::Read<void *>(point, Offsets::OFF_ANCHOR_NODE_RELATIVE_TO);
    if ((Game::Read<uint32_t>(relative, Offsets::OFF_LAYOUT_FLAGS) & Offsets::LAYOUT_FLAG_DEFERRED) != 0) {
        BusyPoint busy{point, false, g_busy};
        g_busy = &busy;
        reinterpret_cast<Realize_t>(Offsets::FUN_REGION_LAYOUT_REALIZE)(relative, 0);
        g_busy = busy.outer;
        if (busy.freed) {
            if (g_walk != nullptr)
                g_walk->changed = true;
            return Undefined();
        }
    }

    uint32_t &flags = Game::Ref<uint32_t>(relative, Offsets::OFF_LAYOUT_FLAGS);
    const uint32_t deferred = flags & Offsets::LAYOUT_FLAG_DEFERRED;
    flags &= ~deferred;
    const float value = reinterpret_cast<Coordinate_t>(engine)(point, scale);
    flags |= deferred;
    return value;
}

float __fastcall PointX(void *point, void * /*edx*/, float scale) {
    return Coordinate(Offsets::FUN_ANCHOR_NODE_X, point, scale);
}

float __fastcall PointY(void *point, void * /*edx*/, float scale) {
    return Coordinate(Offsets::FUN_ANCHOR_NODE_Y, point, scale);
}

void *__fastcall DeletePoint(void *point, void * /*edx*/, int flags) {
    for (BusyPoint *busy = g_busy; busy != nullptr; busy = busy->outer) {
        if (busy->point == point)
            busy->freed = true;
    }
    return reinterpret_cast<Delete_t>(Offsets::FUN_ANCHOR_NODE_DELETE)(point, flags);
}

float Walk(uintptr_t engine, void *layout, const int *points, int count) {
    for (int attempt = 0; attempt < kWalkAttempts; ++attempt) {
        PointWalk walk{false, g_walk};
        g_walk = &walk;
        const float value = reinterpret_cast<Walk_t>(engine)(layout, points, count);
        g_walk = walk.outer;
        if (!walk.changed)
            return value;
    }
    return Undefined();
}

float __fastcall WalkX(void *layout, void * /*edx*/, const int *points, int count) {
    return Walk(Offsets::FUN_LAYOUT_POINTS_X, layout, points, count);
}

float __fastcall WalkY(void *layout, void * /*edx*/, const int *points, int count) {
    return Walk(Offsets::FUN_LAYOUT_POINTS_Y, layout, points, count);
}

void PatchCode() {
    constexpr uintptr_t vtable = Offsets::VAR_ANCHOR_VTABLE_WITH_RELATIVE;
    Common::RedirectSlot(vtable + Offsets::VTBL_ANCHOR_NODE_DELETE, Offsets::FUN_ANCHOR_NODE_DELETE,
                         reinterpret_cast<const void *>(&DeletePoint));
    Common::RedirectSlot(vtable + Offsets::VTBL_ANCHOR_NODE_X, Offsets::FUN_ANCHOR_NODE_X,
                         reinterpret_cast<const void *>(&PointX));
    Common::RedirectSlot(vtable + Offsets::VTBL_ANCHOR_NODE_Y, Offsets::FUN_ANCHOR_NODE_Y,
                         reinterpret_cast<const void *>(&PointY));

    const auto walkX = reinterpret_cast<const void *>(&WalkX);
    Common::RedirectCall(Offsets::CALL_LAYOUT_LEFT_POINTS, Offsets::FUN_LAYOUT_POINTS_X, walkX);
    Common::RedirectCall(Offsets::CALL_LAYOUT_CENTER_X_POINTS, Offsets::FUN_LAYOUT_POINTS_X, walkX);
    Common::RedirectCall(Offsets::CALL_LAYOUT_RIGHT_POINTS, Offsets::FUN_LAYOUT_POINTS_X, walkX);
    const auto walkY = reinterpret_cast<const void *>(&WalkY);
    Common::RedirectCall(Offsets::CALL_LAYOUT_TOP_POINTS, Offsets::FUN_LAYOUT_POINTS_Y, walkY);
    Common::RedirectCall(Offsets::CALL_LAYOUT_CENTER_Y_POINTS, Offsets::FUN_LAYOUT_POINTS_Y, walkY);
    Common::RedirectCall(Offsets::CALL_LAYOUT_BOTTOM_POINTS, Offsets::FUN_LAYOUT_POINTS_Y, walkY);
}

// Each pass runs before its GlueXML or FrameXML loads; the patch is idempotent.
const Game::GlueModuleAutoRegister _gluePatch{&PatchCode};
const Game::ModuleAutoRegister _autoreg{&PatchCode};

} // namespace

} // namespace Frame::AnchorResolve
