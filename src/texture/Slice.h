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

// Texture slicing (nine-slice): a texture drawn in nine pieces whose corners
// keep their size. See Slice.cpp.

namespace Texture::Atlas {
struct Info;
}

namespace Texture::Slice {

// SetAtlas's slice step: a sliced atlas sets its margins and mode on the
// texture, any other atlas clears them.
void ApplyAtlas(void *texture, const Atlas::Info &info);

// The texture whose pieces `positions` points into (a layer batch quad's
// position pointer), or null. A sliced texture's quads do not point at its
// own corners (OFF_SIMPLETEXTURE_CORNERS), so a draw hook that finds the
// texture from that pointer asks here first.
void *Owner(const void *positions);

} // namespace Texture::Slice
