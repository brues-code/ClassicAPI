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

// A file in the client's archives, through SFile: the scene's WMO and terrain
// readers open the world's files the way the client does. A missing file
// opens nothing, with no error.

#include "Offsets.h"

#include <cstdint>
#include <vector>

namespace Model::Scene {

class File {
  public:
    explicit File(const char *path) {
        using Open_t = int(__stdcall *)(const char *path, void **file);
        if (reinterpret_cast<Open_t>(Offsets::FUN_SFILE_OPEN)(path, &handle_) == 0)
            handle_ = nullptr;
    }
    ~File() {
        if (handle_ != nullptr)
            reinterpret_cast<uint32_t(__stdcall *)(void *)>(Offsets::FUN_SFILE_CLOSE)(handle_);
    }
    File(const File &) = delete;
    File &operator=(const File &) = delete;

    bool IsOpen() const { return handle_ != nullptr; }
    uint32_t Size() const {
        using Size_t = uint32_t(__stdcall *)(void *file, uint32_t *high);
        return reinterpret_cast<Size_t>(Offsets::FUN_SFILE_GET_FILE_SIZE)(handle_, nullptr);
    }
    bool Read(void *buffer, uint32_t size) {
        using Read_t = int(__stdcall *)(void *file, void *buffer, uint32_t size, uint32_t *read,
                                        void *overlapped, int);
        uint32_t read = 0;
        return handle_ != nullptr &&
               reinterpret_cast<Read_t>(Offsets::FUN_SFILE_READ)(handle_, buffer, size, &read, nullptr,
                                                                0) != 0 &&
               read == size;
    }
    // The whole file; false for a missing, empty or unsized file.
    bool ReadAll(std::vector<uint8_t> &out) {
        if (handle_ == nullptr)
            return false;
        const uint32_t size = Size();
        if (size == 0 || size == 0xFFFFFFFFu)
            return false;
        out.resize(size);
        return Read(out.data(), size);
    }

  private:
    void *handle_ = nullptr;
};

} // namespace Model::Scene
