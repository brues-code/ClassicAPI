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

#include "embed/Resource.h"

#include "encoding/Compress.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <windows.h>

// The linker-defined base of this module — our HMODULE without a lookup.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace Embed {

namespace {

struct Pack {
    std::vector<uint8_t> bytes;
    std::vector<Bytes> files; // views into `bytes`
};

// Split the inflated `pack.bytes` into `pack.files`. The pack opens with an
// index, each file's size as a decimal `\n`-ended line and then an empty
// line, followed by the files' bytes in index order. False unless the sizes
// cover the bytes exactly.
bool Split(Pack &pack) {
    const uint8_t *p = pack.bytes.data();
    const uint8_t *end = p + pack.bytes.size();
    while (p < end && *p != '\n') {
        size_t size = 0;
        while (p < end && *p >= '0' && *p <= '9')
            size = size * 10 + (*p++ - '0');
        if (p == end || *p++ != '\n')
            return false;
        pack.files.push_back({nullptr, size});
    }
    if (p == end)
        return false;
    ++p; // the empty line
    for (Bytes &file : pack.files) {
        if (file.size > static_cast<size_t>(end - p))
            return false;
        file.data = p;
        p += file.size;
    }
    return p == end;
}

} // namespace

Bytes File(int id, size_t index) {
    static std::unordered_map<int, Pack> packs;
    auto it = packs.find(id);
    if (it == packs.end()) {
        const HMODULE self = reinterpret_cast<HMODULE>(&__ImageBase);
        HRSRC info = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
        if (info == nullptr)
            return {nullptr, 0};
        HGLOBAL handle = LoadResource(self, info);
        if (handle == nullptr)
            return {nullptr, 0};
        Pack pack;
        if (!Encoding::Compress::Inflate(static_cast<const uint8_t *>(LockResource(handle)),
                                         SizeofResource(self, info),
                                         Encoding::Compress::kGzipWindowBits, pack.bytes))
            return {nullptr, 0};
        pack.bytes.shrink_to_fit();
        if (!Split(pack))
            return {nullptr, 0};
        // Moving the vector keeps its buffer, so the views stay valid.
        it = packs.emplace(id, std::move(pack)).first;
    }
    const std::vector<Bytes> &files = it->second.files;
    return index < files.size() ? files[index] : Bytes{nullptr, 0};
}

} // namespace Embed
