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
// create ONE engine-managed CSimpleTexture, draw it every frame (offscreen,
// 1px), and never touch it again — its draw is the live reference that keeps the
// texture hot in VRAM for the quads to bind. Zero churn, no per-icon regions.

namespace Text::InlineTexturePool {

// Ensure a residency holder exists for `path`. Cheap + idempotent: records the
// path; the holder is created once on the next WorldTick. Safe to call from the
// paint hook (records a string only, no engine calls).
void Hold(const char *path);

// Called from FrameScript_Initialize_h BEFORE the /reload teardown: the reload
// destroys every holder texture (regions die with their frames), so drop the
// pointers without touching them — holders rebuild lazily as icons re-draw.
void PrepareForReload();

// Diagnostics: number of live holders.
int HolderCount();

} // namespace Text::InlineTexturePool
