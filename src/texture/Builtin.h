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

namespace Texture::Builtin {

// An embedded image: level 0 as 0xAARRGGBB texels (BGRA8888 in memory), row
// by row, kept for the life of the DLL.
struct Image {
    uint32_t width;
    uint32_t height;
    const uint32_t *texels;
};

// The embedded BLP at the game path `path` (either slash, any case), decoded
// on the first call; null when none is embedded there or it does not decode.
// A texture load of the path that finds no file gets the same image.
const Image *Find(const char *path);

} // namespace Texture::Builtin
