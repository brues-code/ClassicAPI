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

#include "storm/File.h"

#include <cstddef>

namespace Bindings::Inject {

// `FUN_FILE_READ` shape — Storm-style: __stdcall, returns 1 on success
// with the buffer allocated via SMemAlloc into `*outBuf` (size in
// `*outSize`, plus `extraBytes` of trailing zero padding).
using FileReadFn = Storm::File::ReadFn;

// Path-matches `Interface\FrameXML\Bindings.xml`. On match: fetches
// the real file via `original`, splices our ClassicAPI bindings into
// the TARGETING block, and returns `true` with `*outBuf` / `*outSize`
// set to a fresh Storm buffer the caller can SMemFree normally.
// Returns `false` for any other path — caller should fall through to
// its own logic.
bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn original);

} // namespace Bindings::Inject
