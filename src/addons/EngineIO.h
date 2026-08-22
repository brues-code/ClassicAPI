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

// One home for the engine file-I/O and Storm-allocator function-pointer types
// the addon modules call. These are ABI-critical: FUN_FILE_READ /
// FUN_FILE_EXISTS / the Storm allocator are all `__stdcall` (callee cleans the
// stack), and declaring one `__cdecl` by mistake drifts ESP a few bytes per
// call and kills the process with no crash log. The shape lived in ~6 copies
// across the addon files; centralize it so the convention cannot drift.
namespace AddOns::EngineIO {

// FUN_FILE_READ (0x00648620) — __stdcall, callee cleans 28 bytes (RET 0x1C).
// arg0 is an optional archive handle (0 = merged VFS); outSize may be null.
using FileReadFn = int(__stdcall *)(int unused, const char *path, void **outBuf,
                                    size_t *outSize, size_t extraBytes,
                                    int flag1, int flag2);

// FUN_FILE_EXISTS (0x00648A30) — __stdcall, RET 8.
using FileExistsFn = int(__stdcall *)(const char *path, int mode);

// FUN_STORM_SMEM_ALLOC / FUN_STORM_SMEM_FREE — __stdcall, RET 0x10. Buffers
// FUN_FILE_READ hands out are freed with SMemFreeFn; a buffer we allocate with
// SMemAllocFn is freed cleanly by the engine's own SMemFree in turn.
using SMemAllocFn = void *(__stdcall *)(size_t size, const char *file, int line, int flags);
using SMemFreeFn = void(__stdcall *)(void *buf, const char *file, int line, int flags);

} // namespace AddOns::EngineIO
