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

// Called from the reload/teardown hooks (FrameScript_Initialize + world->glue):
// the reload destroys every addon Texture and the region pool reuses their
// pointers, so forget the per-region mask map. (The region ctor co-hook also
// clears an individual entry on reuse; this drops them all in one shot.)
void PrepareForReload();

// Whether any region currently carries a mask — i.e. whether the masked-draw
// path can bind anything on units 1..7 at all. False means this module has not
// touched a texture stage since the last reload, which is what
// Texture::MipAccounting reports to rule the mask path in or out of a
// zero-dimension texture without anyone having to reproduce the freeze.
bool AnyMaskActive();

} // namespace Texture::Mask
