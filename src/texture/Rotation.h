// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

namespace Texture::Rotation {

// Clears the per-region rotation table. Called from DllMain's reload / glue
// teardown paths: addon textures are destroyed on `/reload` and world→glue, and
// the region pool reuses their addresses, so a stale entry must not survive to
// spuriously rotate a new texture that happens to land on the same pointer.
void PrepareForReload();

} // namespace Texture::Rotation
