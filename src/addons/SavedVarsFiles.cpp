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

#include "addons/SavedVarsFiles.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace AddOns::SavedVarsFiles {

namespace {

constexpr char kBak[] = ".bak";
constexpr char kTmp[] = ".tmp";

// FUN_FILE_EXISTS, the engine's file test (through its VFS).
using Exists_t = int(__stdcall *)(const char *path, int mode);

int Exists(const char *path) {
    return reinterpret_cast<Exists_t>(Offsets::FUN_FILE_EXISTS)(path, 1);
}

using ResolveReqDeps_t = uint8_t *(__fastcall *)(const char *name);

// The addon a SavedVariables path belongs to: its `<Name>.lua` file name.
const uint8_t *RecordOfPath(const char *path) {
    const char *file = path;
    for (const char *p = path; *p; ++p)
        if (*p == '\\' || *p == '/') file = p + 1;
    const size_t len = std::strlen(file);
    if (len <= 4) return nullptr;
    const std::string name(file, len - 4); // less ".lua"
    uint8_t *reqDesc =
        reinterpret_cast<ResolveReqDeps_t>(Offsets::FUN_ADDON_RESOLVE_REQ_DEPS)(name.c_str());
    return reqDesc != nullptr ? reqDesc - Offsets::OFF_ADDON_REQDEPS_DESC : nullptr;
}

// The loader's file tests; the path buffer is the loader's own, 0x104 bytes.
// The plain file runs whenever it exists, as on 1.12. The backup runs only for
// a scope the addon declares, as 3.80.2 reads only declared scopes: a backup
// left from a scope the addon no longer has is never loaded.
int LoaderExists(char *path, size_t countOffset) {
    if (Exists(path) != 0) return 1;
    const uint8_t *record = RecordOfPath(path);
    if (record == nullptr || *reinterpret_cast<const uint32_t *>(record + countOffset) == 0)
        return 0;
    return ResolveExisting(path, 0x104) ? 1 : 0;
}

int __stdcall AccountExists(char *path, int) {
    return LoaderExists(path, Offsets::OFF_ADDON_SAVEDVARS_COUNT);
}

int __stdcall CharacterExists(char *path, int) {
    return LoaderExists(path, Offsets::OFF_ADDON_SAVEDVARS_PERCHAR_COUNT);
}

// The OS wrappers the writer uses (UTF-8 paths, converted for the W calls).
using OsCreate_t = void *(__fastcall *)(const char *path, uint32_t access, uint32_t share,
                                        uint32_t disposition, uint32_t attributes,
                                        uint32_t extra);
using OsClose_t = void(__fastcall *)(void *handle);
using OsPath_t = int(__fastcall *)(const char *path);
using OsMove_t = int(__fastcall *)(const char *from, const char *to);
using WriteVariable_t = void(__fastcall *)(void *handle, const char *name);
using OsWrite_t = int(__fastcall *)(void *handle, const void *data, uint32_t size,
                                    uint32_t *written);

int OsExists(const char *path) {
    return reinterpret_cast<OsPath_t>(Offsets::FUN_OS_FILE_EXISTS)(path);
}
void OsDelete(const char *path) {
    reinterpret_cast<OsPath_t>(Offsets::FUN_OS_DELETE_FILE)(path);
}
int OsMove(const char *from, const char *to) {
    return reinterpret_cast<OsMove_t>(Offsets::FUN_OS_MOVE_FILE)(from, to);
}

// The file the writer has open: its real path. The writer opens, fills and
// closes one file at a time.
std::string g_target;
void *g_handle = nullptr;

// 3.80.2's writer streams through a 0x1000-byte buffer. 1.12 writes each
// variable as several WriteFile calls of a few bytes (CR LF, the name, " =
// ", then the value in pieces), straight to the file.
constexpr size_t kBufferSize = 0x1000;
std::vector<char> g_buffer;
bool g_writeFailed = false;

bool OsWriteAll(const void *data, uint32_t size) {
    uint32_t written = 0;
    return reinterpret_cast<OsWrite_t>(Offsets::FUN_OS_WRITE_FILE)(g_handle, data, size,
                                                                  &written) != 0 &&
           written == size;
}

void Flush() {
    if (g_buffer.empty()) return;
    if (!OsWriteAll(g_buffer.data(), static_cast<uint32_t>(g_buffer.size())))
        g_writeFailed = true;
    g_buffer.clear();
}

// The serializer's and the writer's WriteFile calls. Only the file being saved
// is buffered; the same serializer also writes the flat SavedVariables.lua.
int __fastcall BufferedWrite(void *handle, const void *data, uint32_t size,
                             uint32_t *written) {
    if (handle == nullptr || handle != g_handle)
        return reinterpret_cast<OsWrite_t>(Offsets::FUN_OS_WRITE_FILE)(handle, data, size,
                                                                      written);
    if (g_buffer.size() + size > kBufferSize) Flush();
    if (size >= kBufferSize) {
        if (!OsWriteAll(data, size)) g_writeFailed = true;
    } else {
        const char *bytes = static_cast<const char *>(data);
        g_buffer.insert(g_buffer.end(), bytes, bytes + size);
    }
    if (written != nullptr) *written = size;
    return 1;
}

// The writer's "old file to .bak" rename, before it writes: the backup is
// made after the write instead.
int __fastcall SkipBackup(const char *, const char *) { return 1; }

void *__fastcall CreateTemp(const char *path, uint32_t access, uint32_t share,
                            uint32_t disposition, uint32_t attributes, uint32_t extra) {
    g_target = path;
    const std::string temp = g_target + kTmp;
    void *handle = reinterpret_cast<OsCreate_t>(Offsets::FUN_OS_CREATE_FILE)(
        temp.c_str(), access, share, disposition, attributes, extra);
    g_handle = (handle == reinterpret_cast<void *>(-1)) ? nullptr : handle;
    g_buffer.clear();
    g_buffer.reserve(kBufferSize);
    g_writeFailed = false;
    return handle;
}

bool IsSavableName(const char *name) {
    if (name == nullptr || *name == '\0') return false;
    for (const char *p = name; *p; ++p) {
        const char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return false;
    }
    return true;
}

void __fastcall WriteVariable(void *handle, const char *name) {
    if (IsSavableName(name))
        reinterpret_cast<WriteVariable_t>(Offsets::FUN_SV_WRITE_VARIABLE)(handle, name);
}

void __fastcall CloseAndReplace(void *handle) {
    const bool ours = handle != nullptr && handle == g_handle && !g_target.empty();
    if (ours) Flush();
    reinterpret_cast<OsClose_t>(Offsets::FUN_OS_CLOSE_FILE)(handle);
    if (!ours) return;
    g_handle = nullptr;
    const std::string temp = g_target + kTmp;
    if (g_writeFailed) { // as 3.80.2: an incomplete file never replaces the old one
        OsDelete(temp.c_str());
        g_target.clear();
        return;
    }
    if (OsExists(g_target.c_str())) {
        const std::string backup = g_target + kBak;
        OsDelete(backup.c_str());
        // MoveFileW does not replace: when the old file cannot become the
        // backup, it goes, so the complete new file can take its place.
        if (OsMove(g_target.c_str(), backup.c_str()) == 0)
            OsDelete(g_target.c_str());
    }
    OsMove(temp.c_str(), g_target.c_str());
    g_target.clear();
}

void Register() {
    const auto redirect = [](uintptr_t site, uintptr_t expected, const void *target) {
        Common::RedirectCall(site, expected, target);
    };
    redirect(Offsets::CALL_SV_READ_ACCOUNT_EXISTS, Offsets::FUN_FILE_EXISTS,
             reinterpret_cast<const void *>(&AccountExists));
    for (uintptr_t site : {Offsets::CALL_SV_READ_CHAR_EXISTS, Offsets::CALL_SV_READ_CHAR_NO_REALM_EXISTS})
        redirect(site, Offsets::FUN_FILE_EXISTS, reinterpret_cast<const void *>(&CharacterExists));
    for (uintptr_t site : {Offsets::CALL_SV_WRITE_ACCOUNT_BACKUP, Offsets::CALL_SV_WRITE_CHAR_BACKUP})
        redirect(site, Offsets::FUN_OS_MOVE_FILE, reinterpret_cast<const void *>(&SkipBackup));
    for (uintptr_t site : {Offsets::CALL_SV_WRITE_ACCOUNT_CREATE, Offsets::CALL_SV_WRITE_CHAR_CREATE})
        redirect(site, Offsets::FUN_OS_CREATE_FILE, reinterpret_cast<const void *>(&CreateTemp));
    for (uintptr_t site : {Offsets::CALL_SV_WRITE_ACCOUNT_VARIABLE, Offsets::CALL_SV_WRITE_CHAR_VARIABLE})
        redirect(site, Offsets::FUN_SV_WRITE_VARIABLE, reinterpret_cast<const void *>(&WriteVariable));
    for (uintptr_t site : {Offsets::CALL_SV_WRITE_ACCOUNT_CLOSE, Offsets::CALL_SV_WRITE_CHAR_CLOSE})
        redirect(site, Offsets::FUN_OS_CLOSE_FILE, reinterpret_cast<const void *>(&CloseAndReplace));
    for (uintptr_t site :
         {Offsets::CALL_SV_WRITE_ACCOUNT_LINE_END, Offsets::CALL_SV_WRITE_CHAR_LINE_END,
          Offsets::CALL_SV_SERIALIZE_WRITE_1, Offsets::CALL_SV_SERIALIZE_WRITE_2,
          Offsets::CALL_SV_SERIALIZE_WRITE_3, Offsets::CALL_SV_SERIALIZE_WRITE_4,
          Offsets::CALL_SV_SERIALIZE_WRITE_5, Offsets::CALL_SV_SERIALIZE_WRITE_6,
          Offsets::CALL_SV_SERIALIZE_WRITE_7, Offsets::CALL_SV_SERIALIZE_WRITE_8,
          Offsets::CALL_SV_SERIALIZE_WRITE_9, Offsets::CALL_SV_SERIALIZE_WRITE_10})
        redirect(site, Offsets::FUN_OS_WRITE_FILE, reinterpret_cast<const void *>(&BufferedWrite));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

bool ResolveExisting(char *path, size_t cap) {
    if (path == nullptr) return false;
    if (Exists(path) != 0) return true;
    const size_t len = std::strlen(path);
    if (len + sizeof kBak > cap) return false;
    std::memcpy(path + len, kBak, sizeof kBak);
    if (Exists(path) != 0) return true;
    path[len] = '\0';
    return false;
}

} // namespace AddOns::SavedVarsFiles
