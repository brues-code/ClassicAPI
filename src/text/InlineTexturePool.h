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

// Texture-residency holders for the inline-|T icons.
//
// The icons themselves render as raw GxU quads in the text engine's own space
// (see text/InlineTexture.cpp) — pixel-exact position, but a raw quad binds an
// UNOWNED texture that the engine evicts under VRAM pressure (the flicker on
// scroll / blank on a 2nd client). The engine only keeps a texture resident
// while it draws a region that owns it. So for each distinct texture path we
// create ONE engine-managed CSimpleTexture, drawn every frame, and never touch
// it again — its draw is the live reference that keeps the texture hot for the
// quads to bind. The holder must be FULLY OPAQUE to actually draw (near-zero
// alpha truncates to 0 in the engine's parent×region alpha product and the
// region goes inert); it's invisible by GEOMETRY instead — a quad hanging
// almost entirely off the bottom-left screen edge, sub-pixel sliver on-screen
// (batched and bound, but no pixel center covered). See the .cpp header.

#include <cstdint>
#include <string>
#include <vector>

namespace Text::InlineTexturePool {

// Ensure a residency holder exists for `path`. Cheap + idempotent: records the
// path; the holder is created once on the next WorldTick. Safe to call from the
// paint hook (records a string only, no engine calls).
void Hold(const char *path);

// --- region-mode placement (the 4.3.4 CSimpleEmbeddedTexture model) ---------
//
// One pooled CSimpleTexture per visible inline icon, anchored RELATIVE TO THE
// OWNING FONTSTRING at offsets converted from pen space via the fs+0x7C scale
// bridge (see InlineTexture.cpp's FlushLayout). Engine-drawn → resident by
// construction (no flicker, no holders needed); engine-anchored → tracks the
// fontstring's movement for free.

// One icon's target geometry, in ABSOLUTE anchor-space screen coordinates
// (y up, same units as the region rect at +0x64). The pool converts to
// fs-relative SetPoint offsets when applying.
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

// Hide every placed icon region (mode switch away from regions). Pooled regions
// are kept for reuse.
void HideAll();

// Called from FrameScript_Initialize_h BEFORE the /reload teardown: the reload
// destroys every holder texture (regions die with their frames), so drop the
// pointers without touching them — holders rebuild lazily as icons re-draw.
void PrepareForReload();

// Diagnostics: number of live holders.
int HolderCount();

} // namespace Text::InlineTexturePool
