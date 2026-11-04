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

#include "Common.h"

#include <cstddef>

namespace Embed {

struct Bytes {
    const unsigned char *data;
    size_t size;
};

// The bytes of file `index` in the pack that is RCDATA resource `id` in this
// DLL (both come from the generated `embedded_resources.h`). Each resource
// is one gzipped pack of files (cmake/pack_files.cmake); the first call for
// a resource inflates the whole pack and keeps it, so the bytes stay valid
// for the life of the DLL and must not be freed. `{nullptr, 0}` when the
// resource is missing, does not inflate or split, or has no file `index`.
Bytes File(int id, size_t index);

// The entry of `files`, a manifest in the generated `embedded_resources.h`,
// whose path is the game path `path` (Common::SamePath); null when none is.
template <class Entry, size_t N>
const Entry *FindFile(const Entry (&files)[N], const char *path) {
    for (const Entry &file : files) {
        if (Common::SamePath(path, file.path))
            return &file;
    }
    return nullptr;
}

} // namespace Embed
