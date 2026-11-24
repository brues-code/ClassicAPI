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

#include <cstdint>
#include <vector>

// Mouse input on textures and font strings - see RegionInput.cpp.
namespace Frame::RegionInput {

enum Kind { MOTION, CLICK, WHEEL };

// A region's `<Scripts>` node turns its input on: OnEnter, OnLeave,
// OnMouseDown and OnMouseUp turn on motion and clicks, OnMouseWheel the
// wheel, each unless it says `autoEnableInput="false"`.
void AutoEnable(void *region, const void *scripts);

// True when any region takes some mouse input: the input passes have
// regions to look at.
bool Any();

bool MotionEnabled(const void *region);
bool PropagatesMotion(const void *region);
bool PropagatesClicks(const void *region);

// The visible regions under `point` (DDC) that take `kind`, top-first. For
// CLICK, a region that lets `button` through (SetPassThroughButtons) is left
// out.
void Candidates(Kind kind, const float *point, uint32_t button, std::vector<void *> &out);

// True when the region is visible and `point` (DDC) is inside its rect and
// every clipping frame above it.
bool Under(const void *region, const float *point);

// True when `a` draws over `b`, each a frame or a region: by the owner
// frame's strata and level, then a region over every frame of its level
// (3.80.2's draw-order key, FUN_14058D6B0), then the region's draw layer and
// 1.12's order inside a layer batch. False for two frames: the engine's
// mouse lists order those.
bool Above(const void *a, const void *b);

// The cursor in DDC, the units of the hit rects.
void Cursor(const uint8_t *ctx, float *point);

} // namespace Frame::RegionInput
