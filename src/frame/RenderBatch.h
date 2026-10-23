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

// Geometry a frame draws itself, queued into the layer batch its EmitLayer
// slot is handed — 4.3.4's CRenderBatch::Queue, in the one shape 1.12's batch
// draws: a four-vertex strip (triangles 0-1-2 and 1-2-3). See the Offsets.h
// "Layer batch quads" block.

#include <cstdint>

namespace Frame::RenderBatch {

struct C3Vector {
    float x, y, z;
};

struct C2Vector {
    float x, y;
};

// Blend modes (EGxBlend).
enum Blend : uint32_t {
    BLEND_OPAQUE = 0,
    BLEND_ALPHA_KEY = 1,
    BLEND_ALPHA = 2,
    BLEND_ADD = 3,
};

// Appends one strip of `texture` (an HTEXTURE) to `batch`, the way a
// texture region emits itself (FUN_00772FD0). `color` is one BGRA colour for
// all four vertices. The batch keeps the three pointers and reads them on
// every draw until its layer is rebuilt, so they must stay valid and may be
// rewritten in place to animate. Adds nothing when the texture has no device
// texture.
void QueueStrip(void *batch, void *texture, Blend blend, const C3Vector *positions,
                const C2Vector *texCoords, const uint32_t *color);

} // namespace Frame::RenderBatch
