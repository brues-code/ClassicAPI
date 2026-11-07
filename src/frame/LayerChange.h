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

// The engine's two draw-layer invalidates: what to call after a change to
// what a frame or a region draws, so the next batch rebuild picks it up.

#include "Offsets.h"

namespace Frame::LayerChange {

// CSimpleFrame::NotifyLayerChanged (FUN_FRAME_NOTIFY_LAYER_CHANGED): one of
// the frame's own draw layers.
inline void NotifyFrame(void *frame, int layer) {
    using Fn_t = void(__fastcall *)(void *frame, void *edx, int layer);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRAME_NOTIFY_LAYER_CHANGED)(frame, nullptr, layer);
}

// CScriptRegion::NotifyLayerChanged (FUN_REGION_NOTIFY_LAYER_CHANGED): the
// layer the region draws in.
inline void NotifyRegion(void *region) {
    using Fn_t = void(__fastcall *)(void *region);
    reinterpret_cast<Fn_t>(Offsets::FUN_REGION_NOTIFY_LAYER_CHANGED)(region);
}

} // namespace Frame::LayerChange
