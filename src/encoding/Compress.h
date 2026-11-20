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

// What `Inflate` returns: zlib's own codes, plus the size cap.
constexpr int kInflateDone = 1;       // Z_STREAM_END
constexpr int kInflateCorrupt = -3;   // Z_DATA_ERROR, also a truncated stream
constexpr int kInflateNoMemory = -4;  // Z_MEM_ERROR
constexpr int kInflateTooLarge = -5;  // the output reached Args::kDecompressSizeLimit

// Decompresses `src` with the engine's zlib into `out`, as the 3.80.2
// client's helper (0x141B979C0) does: `windowBits` picks the format, as for
// zlib's `inflateInit2_`; bytes after the end of the stream are ignored.
// Returns kInflateDone, or why it stopped (zlib's code for a failure it
// reports itself).
int Inflate(const uint8_t *src, std::size_t srcLen, int windowBits, std::vector<uint8_t> &out);

} // namespace Encoding::Compress
