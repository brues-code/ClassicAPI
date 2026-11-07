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

#include "Offsets.h"

#include <cstdint>

namespace Frame::Layout {

static_assert(Offsets::OFF_REGION_ANCHOR == Offsets::OFF_FRAME_LAYOUT_SUBOBJECT,
              "a frame's layout sub-object is where a region's is");

// The layout sub-object: `this` for SetPoint and the calls below.
inline void *Of(const void *object) {
    return const_cast<uint8_t *>(static_cast<const uint8_t *>(object) + Offsets::OFF_REGION_ANCHOR);
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

} // namespace Frame::Layout
