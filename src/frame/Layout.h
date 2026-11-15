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

#pragma once

// The layout sub-object every region and frame has (4.3.4's CLayoutFrame, at
// OFF_REGION_ANCHOR, which is OFF_FRAME_LAYOUT_SUBOBJECT for a frame) and the
// engine calls on it. Each call here takes the region or frame itself.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Frame::Layout {

static_assert(Offsets::OFF_REGION_ANCHOR == Offsets::OFF_FRAME_LAYOUT_SUBOBJECT,
              "a frame's layout sub-object is where a region's is");

// The layout sub-object: `this` for SetPoint and the calls below.
inline void *Of(const void *object) {
    return const_cast<uint8_t *>(static_cast<const uint8_t *>(object) + Offsets::OFF_REGION_ANCHOR);
}

// Layout units (DDC, the space rects are stored in) to the device's 0..1
// (NDC) and back: each axis scaled by the screen's (VAR_COORD_DDC_*_SCALE).
// Either out pointer may be null, and may alias its input.
inline void DdcToNdc(float *outX, float *outY, float x, float y) {
    using Fn = void(__fastcall *)(float *, float *, float, float);
    reinterpret_cast<Fn>(Offsets::FUN_COORD_DDC_TO_NDC)(outX, outY, x, y);
}
inline void NdcToDdc(float *outX, float *outY, float x, float y) {
    using Fn = void(__fastcall *)(float *, float *, float, float);
    reinterpret_cast<Fn>(Offsets::FUN_COORD_NDC_TO_DDC)(outX, outY, x, y);
}

// The region or frame a layout sub-object belongs to, for the layout
// vtable's overrides and hooks.
inline void *OwnerOf(void *layout) {
    return static_cast<uint8_t *>(layout) - Offsets::OFF_REGION_ANCHOR;
}

// CLayoutFrame::GetRect (FUN_REGION_GET_RECT): {bottom, left, top, right} in
// layout units. False while the object has no valid rect.
inline bool GetRect(const void *object, float (&rect)[4]) {
    using Fn = int(__thiscall *)(void *, float *);
    return reinterpret_cast<Fn>(Offsets::FUN_REGION_GET_RECT)(Of(object), rect) != 0;
}

// IsResizePending (FUN_REGION_LAYOUT_DIRTY): a layout change waits for the
// layout pass.
inline bool IsResizePending(const void *object) {
    using Fn = int(__thiscall *)(void *);
    return reinterpret_cast<Fn>(Offsets::FUN_REGION_LAYOUT_DIRTY)(Of(object)) != 0;
}

// CLayoutFrame::Resize (FUN_LAYOUT_RESIZE): `now` lays the object out at
// once; otherwise it is marked to be laid out later, the path SetPoint takes.
inline void Resize(void *object, bool now) {
    using Fn = void(__thiscall *)(void *, int);
    reinterpret_cast<Fn>(Offsets::FUN_LAYOUT_RESIZE)(Of(object), now ? 1 : 0);
}

// The edge getters' read (Script GetLeft 0x007A1A19): a pending resize run
// now, then the rect. A hidden object, which the layout pass skips, gets a
// rect this way.
inline bool GetResolvedRect(void *object, float (&rect)[4]) {
    if (IsResizePending(object))
        Resize(object, true);
    return GetRect(object, rect);
}

// The object's effective scale (OFF_REGION_EFFECTIVE_SCALE): its own scale
// times its ancestors'. Not positive only before the object is laid out.
inline float EffectiveScale(const void *object) {
    return *reinterpret_cast<const float *>(static_cast<const uint8_t *>(object) +
                                            Offsets::OFF_REGION_EFFECTIVE_SCALE);
}

// A length in layout units in the object's own units, what GetLeft and
// GetWidth push: out of the effective scale, then to pixels, in Script
// GetLeft's order (0x007A1A4E..0x007A1A6B). The engine divides unguarded; a
// scale that is not positive counts as 1 here.
inline double ToLocalUnits(const void *object, float length) {
    const float scale = EffectiveScale(object);
    return Game::InternalToPixel(scale > 0.0f ? length / scale : length);
}

// The inverse: a length in the object's own units in layout units, the
// conversion Script SetPoint makes for its offsets after the effective scale.
inline float FromLocalUnits(const void *object, double length) {
    const float scale = EffectiveScale(object);
    return Game::PixelToInternal(static_cast<float>(length) * (scale > 0.0f ? scale : 1.0f));
}

// The anchor node in `point`'s slot (FRAMEPOINT_*), or nullptr when the slot
// is unset.
inline void *PointNode(const void *object, int point) {
    return *reinterpret_cast<void *const *>(static_cast<const uint8_t *>(object) +
                                            Offsets::OFF_REGION_ANCHOR_ARRAY +
                                            point * static_cast<int>(sizeof(void *)));
}

// One anchor, as the engine's GetPoint reads it. `relativeLayout` comes from
// the node's own getter (vtable +0x0C): a layout sub-object, the screen's
// (VAR_UI_CONTEXT_PTR) for a point on the screen, or null for the node type
// with no relative. `relPoint` is set only with a relative: that 12-byte
// node {vtable, x, y} has no field for it. Offsets are in layout units.
struct Point {
    void *relativeLayout;
    int relPoint;
    float x;
    float y;
};

// Reads `point`'s anchor. False when the slot is unset.
inline bool GetPoint(const void *object, int point, Point *out) {
    using GetRelative_t = void *(__thiscall *)(void *node);
    auto *node = static_cast<uint8_t *>(PointNode(object, point));
    if (node == nullptr)
        return false;
    const auto *vtable = *reinterpret_cast<const uintptr_t *const *>(node);
    out->relativeLayout = reinterpret_cast<GetRelative_t>(
        vtable[Offsets::VTBL_ANCHOR_NODE_GET_RELATIVE / sizeof(uintptr_t)])(node);
    out->relPoint = out->relativeLayout != nullptr
                        ? *reinterpret_cast<const int *>(node + Offsets::OFF_ANCHOR_NODE_RELATIVE_POINT)
                        : 0;
    out->x = *reinterpret_cast<const float *>(node + Offsets::OFF_ANCHOR_NODE_X);
    out->y = *reinterpret_cast<const float *>(node + Offsets::OFF_ANCHOR_NODE_Y);
    return true;
}

// CLayoutFrame::SetPoint (FUN_REGION_SET_POINT): `point` of `object` to
// `relPoint` of `relativeLayout`, offsets in layout units. The relative is a
// layout sub-object, as the engine stores it: `Of(relative)`, or one an
// anchor node or the XML loader already holds. The engine refuses a null
// relative or the object itself (error 0x57), and `resize` lays the object
// out at once (benilla-re ui/anchors).
inline void SetPoint(void *object, int point, void *relativeLayout, int relPoint, float x,
                     float y, bool resize = true) {
    using Fn = void(__thiscall *)(void *, int, void *, int, float, float, int);
    reinterpret_cast<Fn>(Offsets::FUN_REGION_SET_POINT)(Of(object), point, relativeLayout,
                                                        relPoint, x, y, resize ? 1 : 0);
}

// CLayoutFrame::ClearAllPoints (FUN_LAYOUT_CLEAR_ALL_POINTS): frees all nine
// anchor slots and unlinks the object from each relative.
inline void ClearAllPoints(void *object) {
    using Fn = void(__fastcall *)(void *);
    reinterpret_cast<Fn>(Offsets::FUN_LAYOUT_CLEAR_ALL_POINTS)(Of(object));
}

// The engine's point names (FUN_STRING_TO_FRAME_POINT): "TOPLEFT" and the
// rest to FRAMEPOINT_*. False for a string that names no point.
inline bool StringToPoint(const char *name, int *point) {
    using Fn = int(__fastcall *)(const char *, int *);
    return reinterpret_cast<Fn>(Offsets::FUN_STRING_TO_FRAME_POINT)(name, point) != 0;
}

// FUN_FRAME_POINT_TO_STRING: a FRAMEPOINT_* value's name.
inline const char *PointToString(int point) {
    using Fn = const char *(__fastcall *)(int);
    return reinterpret_cast<Fn>(Offsets::FUN_FRAME_POINT_TO_STRING)(point);
}

} // namespace Frame::Layout
