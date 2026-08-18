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

namespace Text {

// True if `p` looks like a readable in-process pointer (heap/.data range), so the
// text hooks can probe engine structures (fontstrings, text nodes, region rects)
// without faulting on a bad/uninitialized field.
//
// UPPER BOUND IS 4GB-ish, NOT 2GB: this client is Large Address Aware
// (VanillaFixes), so heap allocations above 0x80000000 are valid and common once
// the heap grows. A 0x7FFF0000 cap silently rejected high nodes and fontstrings —
// and because the flush's layout walk BREAKS on an "unreadable" node, one high
// node truncated the walk and every node after it lost its icons (the per-row
// random missing-icon bug). Shared by InlineTexture.cpp and InlineTexturePool.cpp;
// the bound lives in exactly one place to keep the two from drifting.
inline bool LooksReadable(const void *p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x00010000u && a < 0xFFFF0000u;
}

} // namespace Text
