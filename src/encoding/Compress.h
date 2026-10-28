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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Encoding::Compress {

// zlib `windowBits` for a Gzip stream.
constexpr int kGzipWindowBits = 15 + 16;

// Decompress `src` with the engine's zlib into `out`. `windowBits` picks the
// format, as for zlib's `inflateInit2_`. False on a corrupt or truncated
// stream.
bool Inflate(const uint8_t *src, std::size_t srcLen, int windowBits,
             std::vector<uint8_t> &out);

} // namespace Encoding::Compress
