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

// See `File.h`. The open arguments are the ones OpenAccountDataFile
// (0x005AF6B0) passes: GENERIC_READ + OPEN_EXISTING to read, GENERIC_WRITE +
// CREATE_ALWAYS to write, no sharing, FILE_ATTRIBUTE_NORMAL. Before a write it
// creates the folder with FUN_OS_CREATE_DIRECTORY (recursive), as that opener
// does.

#include "os/File.h"

#include "Offsets.h"

#include <cstdint>

namespace Os::File {

namespace {

using Create_t = void *(__fastcall *)(const char *path, uint32_t access, uint32_t share,
                                      uint32_t disposition, uint32_t attributes, uint32_t extra);
using Close_t = void(__fastcall *)(void *file);
using Read_t = int(__fastcall *)(void *file, void *buffer, uint32_t size, uint32_t *read);
using Write_t = int(__fastcall *)(void *file, const void *buffer, uint32_t size,
                                  uint32_t *written);
using Size_t = uint32_t(__fastcall *)(void *file); // low dword of the 64-bit size
using Path_t = int(__fastcall *)(const char *path);
using Move_t = int(__fastcall *)(const char *from, const char *to);
using CreateDirectory_t = int(__fastcall *)(const char *path, int recursive);

constexpr uint32_t kGenericRead = 0x80000000;
constexpr uint32_t kGenericWrite = 0x40000000;
constexpr uint32_t kCreateAlways = 2;
constexpr uint32_t kOpenExisting = 3;
constexpr uint32_t kAttributeNormal = 0x80;
void *const kNoFile = reinterpret_cast<void *>(-1);

void *Open(const char *path, uint32_t access, uint32_t disposition) {
    return reinterpret_cast<Create_t>(Offsets::FUN_OS_CREATE_FILE)(path, access, 0, disposition,
                                                                   kAttributeNormal, 0);
}

void Close(void *file) { reinterpret_cast<Close_t>(Offsets::FUN_OS_CLOSE_FILE)(file); }

void Delete(const char *path) { reinterpret_cast<Path_t>(Offsets::FUN_OS_DELETE_FILE)(path); }

} // namespace

bool ReadOpen(void *file, std::string &out) {
    out.clear();
    if (file == kNoFile)
        return false;
    const uint32_t size = reinterpret_cast<Size_t>(Offsets::FUN_OS_GET_FILE_SIZE)(file);
    out.resize(size);
    uint32_t read = 0;
    const bool ok =
        size == 0 ||
        reinterpret_cast<Read_t>(Offsets::FUN_OS_READ_FILE)(file, out.data(), size, &read) != 0;
    out.resize(ok ? read : 0);
    return ok;
}

bool WriteOpen(void *file, const std::string &data) {
    if (file == kNoFile)
        return false;
    const auto size = static_cast<uint32_t>(data.size());
    uint32_t written = 0;
    return size == 0 || (reinterpret_cast<Write_t>(Offsets::FUN_OS_WRITE_FILE)(
                             file, data.data(), size, &written) != 0 &&
                         written == size);
}

bool Read(const std::string &path, std::string &out) {
    void *file = Open(path.c_str(), kGenericRead, kOpenExisting);
    const bool ok = ReadOpen(file, out);
    if (file != kNoFile)
        Close(file);
    return ok;
}

bool Write(const std::string &path, const std::string &data) {
    const auto slash = path.find_last_of('\\');
    if (slash != std::string::npos)
        reinterpret_cast<CreateDirectory_t>(Offsets::FUN_OS_CREATE_DIRECTORY)(
            path.substr(0, slash).c_str(), 1);

    const std::string temp = path + ".tmp";
    void *file = Open(temp.c_str(), kGenericWrite, kCreateAlways);
    if (file == kNoFile)
        return false;
    const bool ok = WriteOpen(file, data);
    Close(file);
    if (!ok) {
        Delete(temp.c_str());
        return false;
    }
    // MoveFileW does not replace an existing file.
    Delete(path.c_str());
    return reinterpret_cast<Move_t>(Offsets::FUN_OS_MOVE_FILE)(temp.c_str(), path.c_str()) != 0;
}

} // namespace Os::File
