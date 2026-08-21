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

// `## LoadSavedVariablesFirst: 1` — modern TOC directive, backported.
//
// Retail loads a flagged addon's SavedVariables BEFORE its Lua runs, so
// file-scope code (`local db = MyAddonDB`) sees restored config immediately.
// Vanilla 1.12 always does the opposite: the per-addon loader `FUN_0051f240`
// runs the addon's files (`FUN_ADDON_LOAD_FILES`), THEN loads SavedVariables,
// THEN fires `ADDON_LOADED`. So file-scope SV is always nil, and vanilla never
// parses the directive at all.
//
// We co-hook `FUN_ADDON_LOAD_FILES` (the per-addon file load, once per addon,
// in-world). For an addon whose TOC declares `## LoadSavedVariablesFirst`
// (nonzero), load its SavedVariables first — mirroring the engine's OWN path
// construction two steps later in `FUN_0051f240` (verified by disassembly):
//   account : WTF\Account\<account>\SavedVariables\<Name>.lua
//   per-char: WTF\Account\<account>\<realm>\<character>\SavedVariables\<Name>.lua,
//             falling back to WTF\Account\<account>\<character>\SavedVariables\<Name>.lua
// (account = *VAR_ACCOUNT_NAME_PTR, realm = FUN_GET_REALM_NAME,
// character = FUN_GET_LOGIN_ACCOUNT_NAME) — then call the original so the
// files run with SV present. `FUN_LUA_LOAD_FILE` no-ops on a missing file, and
// `FUN_FILE_EXISTS` picks the same per-char variant the engine would.
//
// v1 caveat (documented in docs/API.md): the engine still runs its own SV load
// at the normal step, right after the files. SV files are plain `Var = {...}`
// assignments, so re-running is idempotent — file-scope READS of SV work. The
// one divergence from true LoadSavedVariablesFirst: an addon that MUTATES its
// SV at file scope has that overwritten by the engine's re-load before
// ADDON_LOADED (rare — mutation is almost always post-ADDON_LOADED).

#include "Game.h"
#include "Offsets.h"
#include "addons/Toc.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace AddOns::SavedVarsFirst {

namespace {

constexpr size_t NPOS = static_cast<size_t>(-1);

// The hooked FUN_FILE_READ (so a flavor TOC's flag is read — FlavorToc
// applies); __stdcall, RET 0x1C (see addons/Embedded.cpp).
using FileReadFn = int(__stdcall *)(int unused, const char *path, void **outBuf,
                                    size_t *outSize, size_t extraBytes,
                                    int flag1, int flag2);
using SMemFreeFn = void(__stdcall *)(void *buf, const char *file, int line, int flags);
using LoadTocFilesFn = uint32_t(__fastcall *)(char *tocPath, int *a2, int *a3);
using LuaLoadFileFn = uint32_t(__fastcall *)(const char *path, void *a2, void *a3);
using FileExistsFn = int(__stdcall *)(const char *path, int mode);
using NameFn = const char *(*)(); // no-arg readers (return in EAX)

LoadTocFilesFn g_orig = nullptr;

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

bool EqCI(const char *s, size_t n, const char *lit) {
    for (size_t i = 0; i < n; ++i)
        if (lit[i] == '\0' || Lower(s[i]) != Lower(lit[i])) return false;
    return lit[n] == '\0';
}

// Extract "<Name>" from an addon base-TOC path `…\AddOns\<Name>\<Name>.toc`.
// Returns false for anything else (FrameXML.toc, nested paths, …) so the
// SavedVariables reorder only touches real addon loads.
bool AddonNameFromToc(const char *path, char *out, size_t outSize) {
    const size_t len = std::strlen(path);
    if (len < 4 || !(path[len - 4] == '.' && Lower(path[len - 3]) == 't' &&
                     Lower(path[len - 2]) == 'o' && Lower(path[len - 1]) == 'c'))
        return false;
    size_t lastSep = NPOS;
    for (size_t i = len; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { lastSep = i; break; }
    if (lastSep == NPOS) return false;
    size_t prevSep = NPOS;
    for (size_t i = lastSep; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { prevSep = i; break; }
    if (prevSep == NPOS) return false;
    size_t gpSep = NPOS;
    for (size_t i = prevSep; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { gpSep = i; break; }
    if (gpSep == NPOS) return false;
    if (prevSep - (gpSep + 1) != 6 || !EqCI(path + gpSep + 1, 6, "AddOns"))
        return false;
    const size_t nameLen = lastSep - (prevSep + 1);
    if (nameLen == 0 || nameLen + 1 > outSize) return false;
    std::memcpy(out, path + prevSep + 1, nameLen);
    out[nameLen] = '\0';
    return true;
}

// Read the addon's TOC (through the hooked FUN_FILE_READ, so the flavor TOC's
// value is honored) and return whether `## LoadSavedVariablesFirst:` is nonzero.
bool TocWantsSavedVarsFirst(const char *tocPath) {
    auto FileRead =
        reinterpret_cast<FileReadFn>(static_cast<uintptr_t>(Offsets::FUN_FILE_READ));
    void *buf = nullptr;
    size_t size = 0;
    if (FileRead(0, tocPath, &buf, &size, 1, 1, 0) == 0 || buf == nullptr)
        return false;

    bool on = false;
    const char *v = nullptr;
    size_t n = 0;
    if (AddOns::Toc::FindValue(static_cast<const char *>(buf), size,
                               "## LoadSavedVariablesFirst:", &v, &n)) {
        long val = 0;
        bool digit = false;
        for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; ++i) {
            val = val * 10 + (v[i] - '0');
            digit = true;
        }
        on = digit && val != 0;
    }
    reinterpret_cast<SMemFreeFn>(static_cast<uintptr_t>(Offsets::FUN_STORM_SMEM_FREE))(
        buf, __FILE__, __LINE__, 0);
    return on;
}

bool FileExists(const char *path) {
    return reinterpret_cast<FileExistsFn>(
               static_cast<uintptr_t>(Offsets::FUN_FILE_EXISTS))(path, 1) != 0;
}

void RunLuaFile(const char *path) {
    reinterpret_cast<LuaLoadFileFn>(static_cast<uintptr_t>(Offsets::FUN_LUA_LOAD_FILE))(
        path, nullptr, nullptr);
}

// Load the addon's SavedVariables early, mirroring FUN_0051f240's own paths.
void LoadSavedVarsEarly(const char *addonName) {
    const char *account = *reinterpret_cast<const char *const *>(
        static_cast<uintptr_t>(Offsets::VAR_ACCOUNT_NAME_PTR));
    if (account == nullptr || *account == '\0')
        return;

    char path[260];

    // Account-wide (`## SavedVariables`).
    std::snprintf(path, sizeof path, "WTF\\Account\\%s\\SavedVariables\\%s.lua",
                  account, addonName);
    if (FileExists(path))
        RunLuaFile(path);

    // Per-character (`## SavedVariablesPerCharacter`): prefer the realm-scoped
    // file, else the realm-less one — the engine's own fallback order.
    const char *character = reinterpret_cast<NameFn>(
        static_cast<uintptr_t>(Offsets::FUN_GET_LOGIN_ACCOUNT_NAME))();
    if (character == nullptr || *character == '\0')
        return;
    const char *realm = reinterpret_cast<NameFn>(
        static_cast<uintptr_t>(Offsets::FUN_GET_REALM_NAME))();

    if (realm != nullptr && *realm != '\0') {
        std::snprintf(path, sizeof path,
                      "WTF\\Account\\%s\\%s\\%s\\SavedVariables\\%s.lua", account,
                      realm, character, addonName);
        if (FileExists(path)) {
            RunLuaFile(path);
            return;
        }
    }
    std::snprintf(path, sizeof path, "WTF\\Account\\%s\\%s\\SavedVariables\\%s.lua",
                  account, character, addonName);
    if (FileExists(path))
        RunLuaFile(path);
}

uint32_t __fastcall LoadTocFiles_h(char *tocPath, int *a2, int *a3) {
    char name[128];
    if (tocPath != nullptr && AddonNameFromToc(tocPath, name, sizeof name) &&
        TocWantsSavedVarsFirst(tocPath)) {
        LoadSavedVarsEarly(name);
    }
    return g_orig(tocPath, a2, a3);
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_ADDON_LOAD_FILES,
    reinterpret_cast<void *>(&LoadTocFiles_h),
    reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace AddOns::SavedVarsFirst
