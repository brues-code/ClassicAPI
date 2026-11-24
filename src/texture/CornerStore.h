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

#include "AutoList.h"
#include "frame/Layout.h"

// Shared observer for a texture's corner store (FUN_REGION_STORE_CORNERS),
// which writes the drawn quad from a rect: every layout of the texture
// (FUN_TEXTURE_ON_FRAME_SIZE_CHANGED), a coordinate change on a texture with
// SetTexCoordModifiesRect on, and the stores ClassicAPI makes itself.
// Listeners run after the corners are written.
//
// MinHook allows ONE hook per address and more than one feature follows the
// quad (Texture::Transform's rotation, Texture::Tile's repeat count), so this
// module owns the hook and fans out. Declare a file-scope
//   static const Texture::CornerStore::AutoSubscribe _sub{&OnCornersStored};
namespace Texture::CornerStore {

// The rect the corner store is given, made from the region's layout rect
// `rect` in place: cropped by the texcoords
// (FUN_REGION_TEXCOORD_CROP) only when SetTexCoordModifiesRect is on, as the
// texture's layout callback and the layout-resolve store (FUN_00770670) do.
// With the flag clear, a partial texcoord draws at the region's full size.
void CropInput(void *region, Frame::Layout::Rect &rect);

// Writes the texture's quad from its layout rect (OFF_REGION_RECT), cropped
// by CropInput, through the corner store: what the texture's layout callback
// writes, for a caller that changed what the quad depends on (the crop flag,
// the coordinates) without a layout that would rewrite it.
void StoreFromRect(void *texture);

using Listener = void (*)(void *region);

using AutoSubscribe = AutoList<struct AutoSubscribeTag, Listener>;

} // namespace Texture::CornerStore
