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

// SetIgnoreParentAlpha on textures and font strings (IgnoreParentAlpha.cpp).
namespace Frame::IgnoreParentAlpha {

// For its lifetime, an ignoring region's owner frame reads as opaque
// (OFF_FRAME_ALPHA 0xFF): wrap a recolor that reads the owner's alpha in it.
// The region colour fold has its own; the font string's recolor, which also
// reads the byte for its shadow, is hooked by Text::InlineTexturePool, which
// runs it inside one. Does nothing for a region that does not ignore.
// Nestable.
class OwnerOpaque {
  public:
    explicit OwnerOpaque(void *region);
    ~OwnerOpaque();
    OwnerOpaque(const OwnerOpaque &) = delete;
    OwnerOpaque &operator=(const OwnerOpaque &) = delete;

  private:
    uint8_t *m_byte = nullptr;
    uint8_t m_saved = 0;
};

} // namespace Frame::IgnoreParentAlpha
