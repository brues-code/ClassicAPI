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

// Storm's heap (Offsets.h, at FUN_STORM_SMEM_ALLOC): the blocks the engine
// allocates and frees itself. A block the engine will free must come from
// Alloc, and a block the engine hands over is freed with Free; SMemFree
// rejects any other pointer (ERROR #124). `file` and `line` label the block
// in Storm's bookkeeping.

#include "Offsets.h"

#include <cstddef>

namespace Storm {

// SMemAlloc: null when out of memory.
inline void *Alloc(size_t size, const char *file, int line, bool zeroFill = false) {
    using Fn = void *(__stdcall *)(size_t, const char *, int, int);
    return reinterpret_cast<Fn>(Offsets::FUN_STORM_SMEM_ALLOC)(
        size, file, line, zeroFill ? Offsets::STORM_SMEM_FLAG_ZERO_FILL : 0);
}

// SMemFree.
inline void Free(void *ptr, const char *file, int line) {
    using Fn = int(__stdcall *)(void *, const char *, int, int);
    reinterpret_cast<Fn>(Offsets::FUN_STORM_SMEM_FREE)(ptr, file, line, 0);
}

} // namespace Storm
