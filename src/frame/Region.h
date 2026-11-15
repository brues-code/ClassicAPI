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

// Bare pooled regions, built the way Script_CreateTexture / Script_CreateFontString
// build theirs: the class pool's allocator, then the class constructor with the
// parent frame (null for none), draw layer and sublayer. Nothing else is set.
namespace Frame::Region {

// A CSimpleTexture, or nullptr when the pool is out of memory.
void *CreateTexture(void *parent, int layer, int sublayer);

// A CSimpleFontString, or nullptr when the pool is out of memory.
void *CreateFontString(void *parent, int layer, int sublayer);

// A region's vertex colour, packed {b, g, r, a} (FUN_REGION_GET_COLOR):
// white when it has none of its own, and a gradient reads back as white.
uint32_t Color(const void *region);

// Sets a region's vertex colour (FUN_REGION_SET_COLOR), for a texture or a
// font string. The setter does nothing when the one entry it would store is
// the region's colour already, and shrinks a gradient's four entries to one.
void SetColor(void *region, uint32_t bgra);

// Sets only the alpha of the vertex colour. An unchanged alpha writes
// nothing: the getter reads a gradient back as white, so a write would
// flatten it (benilla-re ui/texture, "Setting a vertex colour").
void SetAlpha(void *region, uint8_t alpha);

} // namespace Frame::Region
