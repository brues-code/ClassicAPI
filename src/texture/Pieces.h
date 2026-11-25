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

#include "frame/RenderBatch.h"

#include <cstdint>

// A texture drawn as more than its one quad: the slicing of Texture::Slice and
// the radial fill of Texture::Radial. Each keeps arrays of quads (four
// positions, coordinates and colours each, corner order UL, LL, UR, LR) that
// the texture's batch records point at, and works them out again at each draw
// of the batch from what the texture holds then.
//
// This module owns what they share: emitting the extra records, the batch
// draw (FUN_FRAME_LAYER_RENDER, one hook for both), and the lookup from a
// record's position pointer back to its texture (Texture::Mask uses it).
namespace Texture::Pieces {

using Frame::RenderBatch::C2Vector;
using Frame::RenderBatch::C3Vector;

// One texture's quad arrays, `capacity` quads each. Never reallocated: batch
// records point into them until their layer is rebuilt.
struct Arrays {
    C3Vector *positions = nullptr;
    C2Vector *coords = nullptr;
    uint32_t *colors = nullptr;
    int capacity = 0;
};

// What an emit gave the arrays: how many records point at them, and the
// texture's own colours as its first record had them (one colour, stride 0,
// shared by every quad; or one per corner, for a gradient).
struct Emitted {
    int records = 0;
    const uint32_t *srcColor = nullptr;
    uint32_t colorStride = 0;
};

// Works a texture's quads out again before its batch draws; `block` is what
// Add was given.
using Refresh = void (*)(void *block);

// Registers `arrays`, owned by `texture`, under its first position.
void Add(const Arrays &arrays, void *texture, Refresh refresh, void *block);
// Unregisters them (a batch may still point at them until its rebuild, so the
// owner keeps the memory).
void Remove(const Arrays &arrays);

// Inside a texture's emit (Texture::Emit, after the engine appended its one
// record): appends records until `needed` point at `arrays` and returns what
// the first held. The records read their colours from `arrays` when the
// texture has a gradient, or always with `ownColors` (one colour per vertex,
// stride 4), which the owner then fills from `srcColor`.
Emitted Emit(void *texture, void *batch, int needed, const Arrays &arrays, bool ownColors = false);

// The texture whose quads `positions` points into (a batch record's position
// pointer), or null. Such a record does not point at the texture's own
// corners (OFF_SIMPLETEXTURE_CORNERS), so a draw hook that finds the texture
// from that pointer asks here first.
void *Owner(const void *positions);

// Marks the texture's layer changed (FUN_REGION_NOTIFY_LAYER_CHANGED): its
// batch is rebuilt and the texture emitted again.
void NotifyLayer(void *texture);

// The point at (u, v) of a quad given in corner order UL, LL, UR, LR, u along
// UL->UR and v along UL->LL; the colour channel by channel.
C3Vector Blend(const C3Vector *c, float u, float v);
C2Vector Blend(const C2Vector *c, float u, float v);
uint32_t BlendColor(const uint32_t *c, float u, float v);

} // namespace Texture::Pieces
