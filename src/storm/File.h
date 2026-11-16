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

// The engine's whole-file reader and exists check over its virtual file
// system. Both are `__stdcall` (callee cleans the stack): declaring one
// `__cdecl` drifts ESP a few bytes per call and kills the process with no
// crash log. AddOns::Embedded hooks both, so every call here sees the
// embedded addon's files.

#include "Offsets.h"

#include <cstddef>

namespace Storm::File {

// FUN_FILE_READ (0x00648620), RET 0x1C. `archive` 0 reads the merged VFS;
// `outSize` may be null; the buffer is a Storm block with `extraBytes`
// trailing zeros. Returns 1 on success.
using ReadFn = int(__stdcall *)(int archive, const char *path, void **outBuf, size_t *outSize,
                                size_t extraBytes, int flag1, int flag2);

// FUN_FILE_EXISTS (0x00648A30), RET 8: nonzero when the file exists.
using ExistsFn = int(__stdcall *)(const char *path, int mode);

// Reads `path` whole from the merged VFS. `extraBytes` trailing zeros (1
// makes the buffer a C string); free the buffer with Storm::Free. Passes
// flag1 = 1, flag2 = 0, as every engine caller does.
inline bool Read(const char *path, void **outBuf, size_t *outSize, size_t extraBytes) {
    *outBuf = nullptr;
    return reinterpret_cast<ReadFn>(Offsets::FUN_FILE_READ)(0, path, outBuf, outSize, extraBytes, 1,
                                                            0) != 0 &&
           *outBuf != nullptr;
}

// Whether `path` exists, in mode 1, as the addon and SavedVariables paths ask.
inline bool Exists(const char *path) {
    return reinterpret_cast<ExistsFn>(Offsets::FUN_FILE_EXISTS)(path, 1) != 0;
}

} // namespace Storm::File
