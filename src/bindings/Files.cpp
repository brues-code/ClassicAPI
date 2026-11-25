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

#include "bindings/Files.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "storm/File.h"
#include "storm/Memory.h"

#include <cstdint>

namespace Bindings::Files {

namespace {

bool ValidSet(int set) { return set >= 0 && set < Offsets::BINDING_SET_COUNT; }

// --- Bindings.xml ------------------------------------------------------------

using LoadFile_t = int(__fastcall *)(void *mgr, void *edx, const char *path, void *md5,
                                     void *status);
LoadFile_t g_loadFileOriginal = nullptr;

using TreeLoad_t = void *(__fastcall *)(const char *data, uint32_t size);
using TreeFree_t = void(__fastcall *)(void *tree);

// The file parsed once for every subscriber.
void RunLoadFileSubscribers(const char *path, void *status) {
    if (OnLoadFile::head == nullptr)
        return;
    void *data = nullptr;
    size_t size = 0;
    if (!Storm::File::Read(path, &data, &size, 0))
        return;
    void *tree = reinterpret_cast<TreeLoad_t>(Offsets::FUN_XML_TREE_LOAD)(
        static_cast<const char *>(data), static_cast<uint32_t>(size));
    Storm::Free(data, __FILE__, __LINE__);
    if (tree == nullptr)
        return; // the engine loader reports the parse error
    if (const void *root = Game::Read<const void *>(tree, 0))
        for (auto *s = OnLoadFile::head; s != nullptr; s = s->next)
            s->fn(path, root, status);
    reinterpret_cast<TreeFree_t>(Offsets::FUN_XML_TREE_FREE)(tree);
}

// The status the engine loader is given: a CStatus whose only used slot,
// Add (vtable +0xC), forwards to the real status. Load passes at most two
// string arguments after the format, and Add is __cdecl, so taking exactly
// two is safe whatever the format consumes.
struct LoadStatus {
    const void *const *vtable;
    void *real;
};

bool IsKnownNode(const char *name) {
    for (auto *n = KnownNode::head; n != nullptr; n = n->next)
        if (Ascii::EqualCI(name, n->fn))
            return true;
    return false;
}

void __cdecl LoadStatusAdd(LoadStatus *self, int level, const char *fmt, const char *a,
                           const char *b) {
    if (fmt == reinterpret_cast<const char *>(Offsets::STR_BINDINGS_UNKNOWN_NODE) &&
        a != nullptr && IsKnownNode(a))
        return; // a node a subscriber reads
    using Add_t = void(__cdecl *)(void *status, int level, const char *fmt, ...);
    const void *vtable = Game::Read<const void *>(self->real, 0);
    Game::Read<Add_t>(vtable, Offsets::VTBL_XML_STATUS_ADD)(self->real, level, fmt, a, b);
}

static_assert(Offsets::VTBL_XML_STATUS_ADD == 3 * sizeof(void *));
const void *const kLoadStatusVtable[4] = {nullptr, nullptr, nullptr,
                                          reinterpret_cast<const void *>(&LoadStatusAdd)};

int __fastcall LoadFile_h(void *mgr, void *edx, const char *path, void *md5, void *status) {
    if (status == nullptr)
        status = reinterpret_cast<void *>(Offsets::VAR_BINDINGS_LOAD_STATUS);
    if (path != nullptr)
        RunLoadFileSubscribers(path, status);
    LoadStatus filtered{kLoadStatusVtable, status};
    return g_loadFileOriginal(mgr, edx, path, md5, &filtered);
}

// --- binding files -------------------------------------------------------------

using LoadSet_t = void(__fastcall *)(int set, const char *text);
LoadSet_t g_loadSetOriginal = nullptr;

void __fastcall LoadSet_h(int set, const char *text) {
    if (ValidSet(set))
        for (auto *s = OnLoadSetBegin::head; s != nullptr; s = s->next)
            s->fn(set);
    g_loadSetOriginal(set, text);
    if (ValidSet(set))
        for (auto *s = OnLoadSetEnd::head; s != nullptr; s = s->next)
            s->fn(set, text);
}

using SaveSet_t = void(__fastcall *)(void *mgr, void *edx, int set, void *file);
SaveSet_t g_saveSetOriginal = nullptr;

void __fastcall SaveSet_h(void *mgr, void *edx, int set, void *file) {
    g_saveSetOriginal(mgr, edx, set, file);
    if (ValidSet(set))
        for (auto *s = OnSaveSet::head; s != nullptr; s = s->next)
            s->fn(set, file);
}

using CopySet_t = void(__fastcall *)(void *mgr, void *edx, int source, int dest);
CopySet_t g_copySetOriginal = nullptr;

void __fastcall CopySet_h(void *mgr, void *edx, int source, int dest) {
    g_copySetOriginal(mgr, edx, source, dest);
    if (ValidSet(source) && ValidSet(dest))
        for (auto *s = OnCopySet::head; s != nullptr; s = s->next)
            s->fn(source, dest);
}

const Game::HookAutoRegister _loadFileHook{Offsets::FUN_BINDINGS_LOAD_FILE,
                                           reinterpret_cast<void *>(&LoadFile_h),
                                           reinterpret_cast<void **>(&g_loadFileOriginal)};
const Game::HookAutoRegister _loadSetHook{Offsets::FUN_BINDINGS_LOAD_SET,
                                          reinterpret_cast<void *>(&LoadSet_h),
                                          reinterpret_cast<void **>(&g_loadSetOriginal)};
const Game::HookAutoRegister _saveSetHook{Offsets::FUN_BINDINGS_SAVE_SET,
                                          reinterpret_cast<void *>(&SaveSet_h),
                                          reinterpret_cast<void **>(&g_saveSetOriginal)};
const Game::HookAutoRegister _copySetHook{Offsets::FUN_BINDINGS_COPY_SET,
                                          reinterpret_cast<void *>(&CopySet_h),
                                          reinterpret_cast<void **>(&g_copySetOriginal)};

} // namespace

void WriteLine(void *file, const std::string &line) {
    using WriteFile_t = int(__fastcall *)(void *file, const void *buffer, uint32_t size,
                                          uint32_t *written);
    uint32_t written = 0;
    reinterpret_cast<WriteFile_t>(Offsets::FUN_OS_WRITE_FILE)(
        file, line.data(), static_cast<uint32_t>(line.size()), &written);
}

} // namespace Bindings::Files
