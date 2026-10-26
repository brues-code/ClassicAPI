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

// Bare pooled regions, built the way Script_CreateTexture / Script_CreateFontString
// build theirs: the class pool's allocator, then the class constructor with the
// parent frame (null for none), draw layer and sublayer. Nothing else is set.
namespace Frame::Region {

// A CSimpleTexture, or nullptr when the pool is out of memory.
void *CreateTexture(void *parent, int layer, int sublayer);

// A CSimpleFontString, or nullptr when the pool is out of memory.
void *CreateFontString(void *parent, int layer, int sublayer);

} // namespace Frame::Region
