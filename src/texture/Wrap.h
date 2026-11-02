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

// A texture's wrap mode per axis, and its tile flags, which repeat an axis
// too. See Wrap.cpp.
namespace Texture::Wrap {

// Repeat or clamp per axis: SetTexture's wrap arguments and the XML
// `hWrapMode` / `vWrapMode` attributes.
void SetModes(void *texture, bool repeatH, bool repeatV);

// The tile flags, recorded without touching the image. Texture::Tile owns what
// they do to the coordinates: it counts against the held image, then Settle
// swaps in a copy of that file with the new repeat, which loads only when next
// drawn.
void SetTiles(void *texture, bool h, bool v);

// Reloads the held image with the repeat the modes and tile flags ask for.
void Settle(void *texture);

// The tile flags; true when either is on.
bool Tiles(const void *texture, bool &h, bool &v);
bool Tiles(const void *texture);

// No modes and no tile flags, the image back on the engine's own wrap, as
// SetAtlas leaves a texture.
void Clear(void *texture);

} // namespace Texture::Wrap
