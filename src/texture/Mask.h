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

// Texture:SetMask backport — see texture/Mask.cpp.

namespace Texture::Mask {

// Whether any region currently carries a mask — i.e. whether the masked-draw
// path can bind anything on units 1..7 at all. False means this module has not
// touched a texture stage since the last reload, which is what
// Texture::MipAccounting reports to rule the mask path in or out of a
// zero-dimension texture without anyone having to reproduce the freeze.
bool AnyMaskActive();

// texture:SetMask(path) on `region`. nullptr or "" clears the mask, and so
// does a file that does not load (the return is then false).
bool SetPath(void *region, const char *path);

// Turns a texture region into a MaskTexture: its own copy of the texture
// vtable (GetObjectType "MaskTexture", no mask methods), shown but never drawn,
// and accepted as a mask.
void MakeMask(void *region);

// The region is a MaskTexture. Null-safe.
bool IsMask(const void *region);

enum class AddResult { Added, Full };

// base:AddMaskTexture(mask). Adding the same mask twice is a no-op; a texture
// that already has 3 masks takes no more (checked first, as 3.80.2's).
AddResult Add(void *base, void *mask);

} // namespace Texture::Mask
