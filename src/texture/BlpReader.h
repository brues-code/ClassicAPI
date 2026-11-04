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

#include "Offsets.h"

#include <cstddef>
#include <cstdint>

namespace Texture {

// The engine's BLP reader, driven exactly as GetPortraitAlphaMask drives it
// (see the FUN_BLP_OPEN block in Offsets.h). Open or parse in the
// constructor, decode on demand, free and reset in the destructor.
class BlpReader {
public:
    // Reads the file at `path` (patch MPQs honoured). Only one reader made
    // this way may be live at a time: the engine keeps the file in one
    // module-static buffer.
    explicit BlpReader(const char *path);
    // Parses `size` bytes of a BLP file that stay valid and unchanged for
    // the reader's life: the open without the read (FUN_BLP_PARSE).
    BlpReader(const void *file, std::size_t size);
    ~BlpReader();
    BlpReader(const BlpReader &) = delete;
    BlpReader &operator=(const BlpReader &) = delete;

    bool IsOpen() const { return open_; }
    uint32_t Width() const;
    uint32_t Height() const;

    // Level 0 as BGRA8888 (alpha in byte 3), valid until destruction; null
    // on failure, and for a DXT5 file, which the engine cannot convert.
    const uint8_t *DecodeBgra();

private:
    void Init();
    void Put(uintptr_t off, uint32_t v);

    uint8_t rec_[Offsets::BLP_RECORD_SIZE];
    bool open_ = false;
    bool decoded_ = false;
};

} // namespace Texture
