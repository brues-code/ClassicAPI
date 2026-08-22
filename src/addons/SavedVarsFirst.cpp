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
// The engine STILL runs its own SavedVariables load at the normal step, right
// after the files. Left alone, that re-load would overwrite any value the
// addon wrote to its SavedVariable at file scope before ADDON_LOADED. So we
// also co-hook FUN_LUA_LOAD_FILE and suppress that one redundant re-load: each
// path we pre-load is remembered, and the engine's imminent load of that exact
// path is skipped once. This makes the behavior match true
// LoadSavedVariablesFirst — file-scope reads AND writes both survive. Our own
// early load calls the trampoline directly, so it is never self-suppressed.
// FUN_LUA_LOAD_FILE loads FILES only (not RunScript string execution), so the
// extra hook is a cold, addon-load-time path.

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"
#include "addons/Toc.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace AddOns::SavedVarsFirst {

namespace {

constexpr size_t NPOS = static_cast<size_t>(-1);

// Engine file I/O via the hooked FUN_FILE_READ (so a flavor TOC's flag is read —
// FlavorToc applies), the exists-check, and the Storm free. See addons/EngineIO.h.
using AddOns::EngineIO::FileExistsFn;
using AddOns::EngineIO::FileReadFn;
using AddOns::EngineIO::SMemFreeFn;
using LoadTocFilesFn = uint32_t(__fastcall *)(char *tocPath, int *a2, int *a3);
using LuaLoadFileFn = uint32_t(__fastcall *)(const char *path, void *a2, void *a3);
using NameFn = const char *(*)(); // no-arg readers (return in EAX)

LoadTocFilesFn g_orig = nullptr;
LuaLoadFileFn g_origLuaLoad = nullptr;

// Full SV paths we pre-loaded whose imminent engine re-load must be skipped
// once. Populated in LoadSavedVarsEarly, drained by LuaLoad_h. Only ever holds
// a handful of entries (one addon's SV files, briefly). Main-thread only.
std::vector<std::string> g_pendingSuppress;

using AddOns::Toc::EqCI;
using AddOns::Toc::Lower;

bool SamePathCI(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; ++a, ++b)
        if (Lower(*a) != Lower(*b)) return false;
    return *a == *b;
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

// True iff the leading integer of a TOC value is nonzero (WoW numeric flag).
bool NonzeroFlag(const char *v, size_t n) {
    long val = 0;
    bool digit = false;
    for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; ++i) {
        val = val * 10 + (v[i] - '0');
        digit = true;
    }
    return digit && val != 0;
}

// The SavedVariables directives that decide what LoadSavedVarsEarly does.
struct SvFlags {
    bool first = false;   // `## LoadSavedVariablesFirst:` is nonzero
    bool account = false; // `## SavedVariables:` declares at least one variable
    bool perChar = false; // `## SavedVariablesPerCharacter:` declares at least one
};

// Read the addon's TOC once (through the hooked FUN_FILE_READ, so a flavor
// TOC's directives are honored) and extract the three flags above.
SvFlags ReadSvFlags(const char *tocPath) {
    SvFlags f;
    auto FileRead =
        reinterpret_cast<FileReadFn>(static_cast<uintptr_t>(Offsets::FUN_FILE_READ));
    void *buf = nullptr;
    size_t size = 0;
    if (FileRead(0, tocPath, &buf, &size, 1, 1, 0) == 0 || buf == nullptr)
        return f;
    const char *b = static_cast<const char *>(buf);

    const char *v = nullptr;
    size_t n = 0;
    if (AddOns::Toc::FindValue(b, size, "## LoadSavedVariablesFirst:", &v, &n))
        f.first = NonzeroFlag(v, n);
    // Gate each SV file on the DECLARATION, not mere file existence — mirroring
    // the engine, which loads the account file iff `## SavedVariables` is
    // declared and the per-char file iff `## SavedVariablesPerCharacter` is.
    // (The trailing `:` keeps "## SavedVariables:" from matching the
    // "## SavedVariablesPerCharacter:" line.) Otherwise a stale SV file from a
    // scope the addon no longer declares would be executed early — Lua the
    // engine never runs — and queued for a suppression the engine never drains.
    f.account = AddOns::Toc::FindValue(b, size, "## SavedVariables:", &v, &n) && n > 0;
    f.perChar =
        AddOns::Toc::FindValue(b, size, "## SavedVariablesPerCharacter:", &v, &n) && n > 0;

    reinterpret_cast<SMemFreeFn>(static_cast<uintptr_t>(Offsets::FUN_STORM_SMEM_FREE))(
        buf, __FILE__, __LINE__, 0);
    return f;
}

bool FileExists(const char *path) {
    return reinterpret_cast<FileExistsFn>(
               static_cast<uintptr_t>(Offsets::FUN_FILE_EXISTS))(path, 1) != 0;
}

// Load a SavedVariables file we found, then mark its path so the engine's own
// re-load of it (right after the addon's files) is suppressed — preserving any
// file-scope write. Calls the trampoline directly so this load is never itself
// suppressed by our LuaLoad_h hook.
void RunLuaFile(const char *path) {
    if (g_origLuaLoad == nullptr)
        return;
    g_origLuaLoad(path, nullptr, nullptr);
    g_pendingSuppress.emplace_back(path);
}

// Load the addon's SavedVariables early, mirroring FUN_0051f240's own paths.
// Each scope loads only when its directive is declared (`hasAccount` /
// `hasPerChar`) AND the file exists — exactly what the engine's own SV step
// would re-load, so every suppress entry we queue is later drained.
void LoadSavedVarsEarly(const char *addonName, bool hasAccount, bool hasPerChar) {
    const char *account = *reinterpret_cast<const char *const *>(
        static_cast<uintptr_t>(Offsets::VAR_ACCOUNT_NAME_PTR));
    if (account == nullptr || *account == '\0')
        return;

    char path[260];

    // Account-wide (`## SavedVariables`).
    if (hasAccount) {
        std::snprintf(path, sizeof path, "WTF\\Account\\%s\\SavedVariables\\%s.lua",
                      account, addonName);
        if (FileExists(path))
            RunLuaFile(path);
    }

    // Per-character (`## SavedVariablesPerCharacter`): prefer the realm-scoped
    // file, else the realm-less one — the engine's own fallback order.
    if (!hasPerChar)
        return;
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
    if (tocPath != nullptr && AddonNameFromToc(tocPath, name, sizeof name)) {
        const SvFlags f = ReadSvFlags(tocPath);
        if (f.first)
            LoadSavedVarsEarly(name, f.account, f.perChar);
    }
    return g_orig(tocPath, a2, a3);
}

// Skip the engine's one redundant SavedVariables re-load per pre-loaded path.
// Every other file load passes straight through. Match is by full path
// (case-insensitive); a path is suppressed exactly once, so nothing else is
// affected. Our own early load bypasses this via the trampoline.
uint32_t __fastcall LuaLoad_h(const char *path, void *a2, void *a3) {
    if (path != nullptr) {
        for (size_t i = 0; i < g_pendingSuppress.size(); ++i) {
            if (SamePathCI(g_pendingSuppress[i].c_str(), path)) {
                g_pendingSuppress.erase(g_pendingSuppress.begin() +
                                        static_cast<std::ptrdiff_t>(i));
                return 0; // suppress — the addon's file-scope SV writes survive
            }
        }
    }
    return g_origLuaLoad(path, a2, a3);
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_ADDON_LOAD_FILES,
    reinterpret_cast<void *>(&LoadTocFiles_h),
    reinterpret_cast<void **>(&g_orig)};

const Game::HookAutoRegister _hookLua{
    Offsets::FUN_LUA_LOAD_FILE,
    reinterpret_cast<void *>(&LuaLoad_h),
    reinterpret_cast<void **>(&g_origLuaLoad)};

} // namespace

} // namespace AddOns::SavedVarsFirst
