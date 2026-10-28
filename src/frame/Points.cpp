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

// Per-point anchor methods and rect validity on the Region registry:
//
//   region:GetPointByName(point)   GetPoint's returns for the anchor at
//                                  `point`, nothing when that slot is empty
//   region:ClearPoint(point)       removes the anchor at `point`
//   region:IsRectValid()           true when the region has a rect
//   region:AdjustPointsOffset(x, y)  adds (x, y) to every anchor's offset
//   region:ClearPointsOffset()     sets every anchor's offset to 0
//
// A layout keeps one anchor per point, in the slot of that point's index
// (benilla-re ui/anchors "Point slots"), so a point name selects a slot
// directly. GetPointByName reads it through GetPoint's own pusher.
//
// ClearPoint is CLayoutFrame::ClearAllPoints (FUN_LAYOUT_CLEAR_ALL_POINTS)
// for one slot: drop this point's bit from the relative frame's dependents
// (FUN_LAYOUT_REMOVE_DEPENDENT), delete the node through its vtable slot 0
// with 1, and NULL the slot. Like ClearAllPoints it does not lay the region
// out; the next anchor or size change does.
//
// AdjustPointsOffset and ClearPointsOffset re-set each anchor through the
// engine's SetPoint (FUN_REGION_SET_POINT) with the anchor's own relative
// frame and point and the new offset, so the region lays out as a script
// SetPoint would. Retail, probed: SetPoint("CENTER", 5, 7), then
// AdjustPointsOffset(1, -1) reads back (6, 6), and ClearPointsOffset then
// reads back (0, 0) - it zeroes the offsets, it does not undo adjustments.
//
// IsRectValid takes the steps of the stock edge getters (Script GetLeft):
// lay out a pending region now, then answer the rect reader's valid bit, so
// it is true exactly when GetLeft answers a number.

#include "Game.h"
#include "Offsets.h"
#include "frame/GetPointGuard.h"

#include <cstdint>

namespace Frame::Points {

namespace {

using StringToPoint_t = int(__fastcall *)(const char *name, int *point);
using RemoveDependent_t = void(__fastcall *)(void *relative, void *edx, void *layout, int mask);
using GetRelative_t = void *(__thiscall *)(void *node);
using DeleteNode_t = void(__thiscall *)(void *node, int flags);
using LayoutDirty_t = int(__fastcall *)(void *layout);
using Resize_t = void(__fastcall *)(void *layout, void *edx, int force);
using GetRect_t = int(__fastcall *)(void *layout, void *edx, float *out);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);

// The point named by argument 2, or -1 after raising the usage error.
int PointArg(void *L, void *region, const char *usage) {
    int point = 0;
    if (!Game::Lua::IsString(L, 2) ||
        !reinterpret_cast<StringToPoint_t>(Offsets::FUN_STRING_TO_FRAME_POINT)(
            Game::Lua::ToString(L, 2), &point)) {
        Game::Lua::Error(L, usage, Game::Lua::ObjectDisplayName(region));
        return -1;
    }
    return point;
}

int __fastcall Script_GetPointByName(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region == nullptr)
        return 0;
    const int point = PointArg(L, region, "Usage: %s:GetPointByName(\"point\")");
    if (point < 0)
        return 0;
    Frame::GetPointGuard::CleanDanglingAnchors(region);
    return Frame::GetPointGuard::PushPoint(L, region, point);
}

int __fastcall Script_ClearPoint(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region == nullptr)
        return 0;
    const int point = PointArg(L, region, "Usage: %s:ClearPoint(\"point\")");
    if (point < 0)
        return 0;
    Frame::GetPointGuard::CleanDanglingAnchors(region);
    auto **slot = reinterpret_cast<uint8_t **>(static_cast<uint8_t *>(region) +
                                               Offsets::OFF_REGION_ANCHOR_ARRAY + point * 4);
    uint8_t *node = *slot;
    if (node == nullptr)
        return 0;
    auto *vtable = Game::Read<uintptr_t *>(node, 0);
    void *relative = reinterpret_cast<GetRelative_t>(
        vtable[Offsets::VTBL_ANCHOR_NODE_GET_RELATIVE / sizeof(uintptr_t)])(node);
    if (relative != nullptr)
        reinterpret_cast<RemoveDependent_t>(Offsets::FUN_LAYOUT_REMOVE_DEPENDENT)(
            relative, nullptr, static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR,
            1 << point);
    reinterpret_cast<DeleteNode_t>(vtable[0])(node, 1);
    *slot = nullptr;
    return 0;
}

// Re-sets every anchor with its offset moved by (dx, dy) in layout units, or
// with no offset when `zero`.
void ShiftPoints(void *region, float dx, float dy, bool zero) {
    Frame::GetPointGuard::CleanDanglingAnchors(region);
    for (int point = 0; point < Offsets::REGION_ANCHOR_SLOT_COUNT; ++point) {
        uint8_t *node = Game::Read<uint8_t *>(region, Offsets::OFF_REGION_ANCHOR_ARRAY + point * 4);
        if (node == nullptr)
            continue;
        auto *vtable = Game::Read<uintptr_t *>(node, 0);
        void *relative = reinterpret_cast<GetRelative_t>(
            vtable[Offsets::VTBL_ANCHOR_NODE_GET_RELATIVE / sizeof(uintptr_t)])(node);
        const int relativePoint = Game::Read<int>(node, Offsets::OFF_ANCHOR_NODE_RELATIVE_POINT);
        const float x = zero ? 0.0f : Game::Read<float>(node, Offsets::OFF_ANCHOR_NODE_X) + dx;
        const float y = zero ? 0.0f : Game::Read<float>(node, Offsets::OFF_ANCHOR_NODE_Y) + dy;
        reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
            static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR, point, relative,
            relativePoint, x, y, 1);
    }
}

int __fastcall Script_AdjustPointsOffset(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: %s:AdjustPointsOffset(x, y)", Game::Lua::ObjectDisplayName(region));
        return 0;
    }
    ShiftPoints(region, Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 2))),
                Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 3))), false);
    return 0;
}

int __fastcall Script_ClearPointsOffset(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region == nullptr)
        return 0;
    ShiftPoints(region, 0.0f, 0.0f, true);
    return 0;
}

int __fastcall Script_IsRectValid(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    if (region == nullptr)
        return 0;
    void *layout = static_cast<uint8_t *>(region) + Offsets::OFF_REGION_ANCHOR;
    if (reinterpret_cast<LayoutDirty_t>(Offsets::FUN_REGION_LAYOUT_DIRTY)(layout) != 0)
        reinterpret_cast<Resize_t>(Offsets::FUN_LAYOUT_RESIZE)(layout, nullptr, 1);
    float rect[4];
    Game::Lua::PushBoolean(
        L, reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(layout, nullptr, rect) != 0);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetPointByName", &Script_GetPointByName},
    {"ClearPoint", &Script_ClearPoint},
    {"IsRectValid", &Script_IsRectValid},
    {"AdjustPointsOffset", &Script_AdjustPointsOffset},
    {"ClearPointsOffset", &Script_ClearPointsOffset},
};

const Game::Doc::Field kPointArg[] = {
    Game::Doc::Req("point", "FramePoint"),
};
const Game::Doc::Field kPointRets[] = {
    Game::Doc::Req("point", "FramePoint"),
    Game::Doc::Req("relativeTo", "ScriptRegion"),
    Game::Doc::Req("relativePoint", "FramePoint"),
    Game::Doc::Req("offsetX", "uiUnit"),
    Game::Doc::Req("offsetY", "uiUnit"),
};
const Game::Doc::Field kValidRets[] = {
    Game::Doc::Req("isValid", "bool"),
};
const Game::Doc::Function kGetPointByName{
    "Returns the anchor at a point, as GetPoint returns it; returns nothing when the region "
    "has no anchor at that point.",
    kPointArg, kPointRets};
const Game::Doc::Function kClearPoint{"Removes the region's anchor at a point.", kPointArg, {}};
const Game::Doc::Function kIsRectValid{
    "Returns true when the region's position and size are resolved from its anchors.",
    {}, kValidRets};
const Game::Doc::Field kOffsetArgs[] = {
    Game::Doc::Req("x", "uiUnit"),
    Game::Doc::Req("y", "uiUnit"),
};
const Game::Doc::Function kAdjustPointsOffset{"Adds an offset to every anchor of the region.", kOffsetArgs,
                                              {}};
const Game::Doc::Function kClearPointsOffset{"Sets the offset of every anchor of the region to 0.", {}, {}};
const Game::Doc::Method g_methodDocs[] = {
    {"AdjustPointsOffset", &kAdjustPointsOffset},
    {"ClearPointsOffset", &kClearPointsOffset},
    {"GetPointByName", &kGetPointByName},
    {"ClearPoint", &kClearPoint},
    {"IsRectValid", &kIsRectValid},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::Points
