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

// Engine-texture rendering for the inline-|T icons, 4.3.4's
// CSimpleEmbeddedTexture model.
//
// One pooled CSimpleTexture per inline icon, anchored RELATIVE TO THE OWNING
// FONTSTRING at offsets converted from pen space (see InlineTexture.cpp's
// ComputePlacementsForNode). As in 4.3.4 the texture has no frame,
// so it is in no draw layer: the fontstring draws it. Its draw call
// (FUN_FONTSTRING_EMIT, 4.3.4's CSimpleFontString::Draw → QueueTextures) adds
// the icons to the layer batch right after its text, so an icon draws exactly
// when its line's text does - shown, hidden, faded out, off screen - in the
// same frame. Engine-drawn → resident by construction; engine-anchored →
// tracks the fontstring's movement. The placements are applied when the
// string is built and again after each move, since the move re-rounds the
// glyph origin (see InlineTexture.cpp's MoveIcons).

#include <cstdint>
#include <string>
#include <vector>

namespace Text::InlineTexturePool {

// One icon's target geometry, in anchor-space units RELATIVE TO THE OWNING
// FONTSTRING's rect min corner (y up, same units as the region rect at +0x64).
// Computed from the laid-out node and the fs rect in one read, so the pool
// applies it verbatim with no apply-time rect read (an apply-time read raced
// the chat relayout after SetText and parked icons off their line: the
// scroll-landing "randomly hidden icon" bug).
struct Placement {
    std::string path;
    float x0, y0, x1, y1;
    uint32_t color;
    float u0, v0, u1, v1;
};

// Exact compare is correct here: unchanged layouts reproduce bit-identical
// floats, and that's precisely the "nothing changed" case Apply skips.
inline bool operator==(const Placement &a, const Placement &b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1 && a.color == b.color &&
           a.u0 == b.u0 && a.v0 == b.v0 && a.u1 == b.u1 && a.v1 == b.v1 && a.path == b.path;
}

// Applies the icon set of a FONTSTRING (the stable entity — text nodes die on
// every SetText, so textures must not be keyed by node) to its textures at
// once and marks its layer for a batch rebuild, so the icons draw with the
// text. A set equal to the one applied is skipped. An empty vector stops the
// fontstring drawing icons. Faults are caught (latches the pool's kill
// switch, re-armed on /reload).
void Apply(void *fs, std::vector<Placement> &&icons);

// The fontstring's built string was replaced: it draws no icons until new
// placements arrive (4.3.4's FreeEmbeddedTextures at the top of UpdateString).
// Its textures stay pooled for them.
void Release(void *fs);

// Called BEFORE the /reload teardown and on glue boot: forgets every
// fontstring and deletes every icon texture at the next tick, after a draw
// whose batches no longer hold them.
void PrepareForReload();

} // namespace Text::InlineTexturePool
