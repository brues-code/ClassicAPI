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

// Path -> HTEXTURE, the load CSimpleTexture::SetTexture (FUN_00770200) makes.

#include <cstdint>

namespace Texture {

// The loaded handle, or nullptr when the file does not load. Linear filter,
// clamped, the flags SetTexture's own call builds. The caller owns one
// reference and drops it with ReleaseHandle.
void *LoadByPath(const char *path);

// Drops one reference on a handle (FUN_HANDLE_RELEASE). Null is a no-op.
void ReleaseHandle(void *handle);

// A one-mip `width` x `height` texture of ARGB8888 `texels`, through the
// engine's own procedural path (FUN_TEXTURE_CREATE_WITH_CALLBACK) with the
// same flags as LoadByPath. The engine reads `texels` whenever it uploads the
// texture, so they must outlive the handle. Never deduplicated: each call is
// a new texture.
void *CreateFromPixels(const char *name, uint32_t width, uint32_t height,
                       const uint32_t *texels);

} // namespace Texture
