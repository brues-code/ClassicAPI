// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

namespace Texture::Transform {

// Drawn-region transform authority. Backs `Texture:SetRotation` /
// `Texture:SetVertexOffset` (4-corner quad rewrite) AND `FontString:SetRotation`
// (glyph-vert rotation) — both are LayeredRegions, so the {angle, pivot}
// storage and the SetRotation/GetRotation Lua surface are shared; only the
// apply-to-geometry step forks by type.

// Rotates a text render node's freshly-baked glyph vertices in place, by the
// rotation stored for its owning CSimpleFontString `fs`, around the vertex
// block's centre. No-op when `fs` has no rotation. MUST be called only right
// after a fresh vertex bake (verts are axis-aligned then, so rotating is
// idempotent-by-construction); the inline-texture DrawBuilder co-hook is that
// point — see src/text/InlineTexture.cpp. Reads the node page-buffer layout in
// Offsets.h (OFF_TEXT_NODE_PAGE_BUFFERS …).
void RotateFontStringNode(void *node, void *fs);

// Scales, then turns (radians, counter-clockwise), a freshly baked text node's
// glyph vertices about the node origin (0, 0) - where 4.3.4's
// CGxString::InitializeTextLine turns them for a string's rotation, and the
// point a larger font height grows from. Shrinks the glyph UVs by the
// engine's half-texel inset when it turns them. Same timing contract as
// RotateFontStringNode: only right after a fresh bake.
void ScaleRotateFontStringNode(void *node, float scale, float angle);

// The stored rotation for `region` (from SetRotation), if any: writes the angle
// (radians, positive = counter-clockwise on screen) and the normalized pivot,
// returns true. False when the region has no rotation. Texture::Mask reads a
// MaskTexture's rotation through this to rotate its clip shape — a mask never
// renders, so its corner rewrite is moot; the {angle, pivot} record is the
// entire effect.
bool GetRotation(void *region, float *angleRad, float *cxN, float *cyN);

// The vertex offsets SetVertexOffset stored for `region`, in its local pixels
// (+y up), by corner BL, TL, BR, TR: true when it has any. Texture::Line moves
// its own corners by them, in the line's frame.
bool VertexOffsets(const void *region, float offX[4], float offY[4]);

// Where `region` draws the corners of the rect {left, right, top, bottom}
// (rect space, y-down: top < bottom), BL, TL, BR, TR: rotated about its pivot,
// then moved by its vertex offsets. The rect's own corners when it has neither.
// Texture::Mask builds a mask's frame from these, as 3.80.2 does from the
// drawn corners (0x14057A2E0).
void TargetCorners(void *region, float left, float right, float top, float bottom, float tx[4],
                   float ty[4]);

} // namespace Texture::Transform
