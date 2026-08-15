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

// Pool of engine-owned `CSimpleTexture` regions rendering inline `|T…|t` icons —
// the 4.3.4 `CSimpleEmbeddedTexture` model ported to 1.12.
//
// Verified against 4.3.4's `UpdateEmbeddedTextures` (FUN_004fd670 →
// FUN_004fd390, CSimpleRender.cpp): an embedded icon is a pooled CSimpleTexture
// configured at text-UPDATE time — SetTexture + a SetPoint anchored to the
// OWNING FONTSTRING + texcoord + Show — then left alone. The engine's region
// draw keeps its texture resident (no flicker), and the anchor system moves it
// whenever the owning line moves (scroll costs nothing). 1.12 chat lines are
// real CSimpleFontStrings (the ScrollingMessageFrame display refresh
// FUN_00788750 drives one per visible line), so the same ownership works for
// chat, tooltips, and standalone FontStrings alike.
//
// Timing: PlaceOwned() only records; ApplyPass() (called from the icon
// publisher's WorldTick) performs all engine mutation off-render, diff-cached
// per slot so an unchanged icon costs nothing.

namespace Text::InlineTexturePool {

// Record one icon anchored to `ownerFs` (a live CSimpleFontString) for this
// pass. Coordinates are owner-local UI px, x right / y down from the owner's
// text origin. (u0,v0)=(left,top), (u1,v1)=(right,bottom) texcoords; color =
// 0xAARRGGBB tint. Returns false if the pass is full.
bool PlaceOwned(void *ownerFs, float x0, float y0, float x1, float y1, const char *path, float u0,
                float v0, float u1, float v1, uint32_t color);

// Apply the recorded pass to the pool (all engine mutation happens here, on the
// WorldTick): per changed slot — reparent to the owner's frame if needed,
// SetTexture, two corner anchors to the owner, realize, texcoord, tint, and
// mirror the owner's shown state. Slots past the pass are hidden.
void ApplyPass();

// Called from FrameScript_Initialize_h BEFORE the /reload teardown. The reload
// destroys every pool texture (regions die with their frames), so all engine
// pointers are dropped WITHOUT touching them and everything rebuilds lazily.
void PrepareForReload();

// Diagnostics.
int PoolReady();
int PoolSize();
int ShownCount();

} // namespace Text::InlineTexturePool
