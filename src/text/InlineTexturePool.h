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

// Engine-region rendering for the inline-|T icons (the 4.3.4
// CSimpleEmbeddedTexture model).
//
// One pooled CSimpleTexture per visible inline icon, anchored RELATIVE TO THE
// OWNING FONTSTRING at offsets converted from pen space via the fs+0x7C scale
// bridge (see InlineTexture.cpp's FlushLayout). Engine-drawn → resident by
// construction (no VRAM-eviction flicker); engine-anchored → tracks the
// fontstring's movement for free.

#include <cstdint>
#include <string>
#include <vector>

namespace Text::InlineTexturePool {

// One icon's target geometry, in anchor-space units RELATIVE TO THE OWNING
// FONTSTRING's rect min corner (y up, same units as the region rect at +0x64).
// Computed entirely at PAINT time — icon coords and fs rect read in the same
// flush, a coherent snapshot — so the pool applies it verbatim with no
// apply-time rect read (an apply-time read raced the chat relayout after
// SetText and parked icons off their line, where the dedup froze them: the
// scroll-landing "randomly hidden icon" bug). Relative offsets are also
// position-invariant, so scrolling a line re-queues nothing.
struct Placement {
    std::string path;
    float x0, y0, x1, y1;
    uint32_t color;
    float u0, v0, u1, v1;
};

// Exact compare is correct here: unchanged layouts reproduce bit-identical
// floats, and that's precisely the "nothing moved" case the dedup wants.
inline bool operator==(const Placement &a, const Placement &b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1 && a.color == b.color &&
           a.u0 == b.u0 && a.v0 == b.v0 && a.u1 == b.u1 && a.v1 == b.v1 && a.path == b.path;
}

// Queue the icon set for a FONTSTRING (the stable entity — text nodes die on
// every SetText, so regions must not be keyed by node). Safe from the paint
// hook: only records data; regions are created/configured on the next
// WorldTick (never mid-render). Identical re-queues are deduped, so calling
// every paint is cheap. An empty vector hides the fontstring's regions; a
// hidden fontstring's regions are hidden automatically each tick.
void QueuePlacements(void *fs, std::vector<Placement> &&icons);

// Called from FrameScript_Initialize_h BEFORE the /reload teardown: the reload
// destroys every icon region (they die with their parent frames), so drop the
// pointers without touching them — regions rebuild lazily as icons re-draw.
void PrepareForReload();

} // namespace Text::InlineTexturePool
