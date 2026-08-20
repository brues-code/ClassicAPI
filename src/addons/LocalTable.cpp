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

// `C_AddOns.GetAddOnLocalTable(name)` — the public, gated view of an addon's
// private namespace table (the `local _, addonTable = ...` second vararg the
// transpiler injects; see luasyntax/AddonNamespace.h).
//
// Retail added this in 11.0 alongside the `## AllowAddOnTableAccess` TOC
// directive: one addon can read another's local table ONLY when the target
// opts in. We mirror that gate:
//   1. the named addon is currently LOADED, and
//   2. its `.toc` declares `## AllowAddOnTableAccess: 1`.
// Either check failing returns nil. The internal preamble path (an addon
// receiving its OWN table) is a different, ungated function (`__addonns`) —
// an addon never needs permission for its own namespace.
//
// Directive spelling verified against the 1.15.8 Classic Era binary (both the
// `AllowAddOnTableAccess:` TOC key and the `GetAddOnLocalTable` name are
// present there). The vanilla 1.12 TOC parser drops unknown `##` directives,
// so we scan the `.toc` ourselves via `FUN_FILE_READ`.

#include "Game.h"
#include "Offsets.h"
#include "luasyntax/AddonNamespace.h"

#include <cstdint>
#include <cstdio>

namespace AddOns::LocalTable {

namespace {

// IS_LOADED is a by-name hash lookup returning the loaded byte (`entry+0x18`);
// passing the name string works because the lookup hashes it. See Offsets.h.
using IsLoadedFn_t = uint8_t(__fastcall *)(const char *name);

// `FUN_FILE_READ` — __stdcall (callee cleans 28 bytes via RET 0x1C). Shape
// copied verbatim from addons/Embedded.cpp; declaring it __cdecl silently
// corrupts the caller's stack. Calling the raw address routes through
// Embedded's file-read hook, which forwards every non-`!!!ClassicAPI` path to
// the original untouched — exactly what we want here.
using FileRead_t = int(__stdcall *)(int unused, const char *path, void **outBuf,
                                    size_t *outSize, size_t extraBytes,
                                    int flag1, int flag2);
using SMemFree_t = void(__stdcall *)(void *buf, const char *file, int line, int flags);

inline char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Case-insensitive compare of `n` bytes. TOC keys are matched loosely — the
// engine treats them case-insensitively and we own this scan (the parser never
// sees the directive), so an author writing a different case still opts in.
bool EqualsCI(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (LowerAscii(a[i]) != LowerAscii(b[i]))
            return false;
    return true;
}

// True iff the addon's `.toc` has a line `## AllowAddOnTableAccess: <nonzero>`.
// The value is read as a TOC boolean flag: parse the leading integer, true iff
// non-zero (so `1` opts in, `0`/absent do not) — matching WoW's numeric TOC
// flag semantics. Not cached: GetAddOnLocalTable is not a hot path.
bool TocAllowsTableAccess(const char *name) {
    // A registered, loaded addon name never carries a path separator; reject
    // any that does so the read stays bounded to the addon's own folder.
    for (const char *p = name; *p; ++p)
        if (*p == '\\' || *p == '/')
            return false;

    char path[300];
    std::snprintf(path, sizeof path, "Interface\\AddOns\\%s\\%s.toc", name, name);

    void *buf = nullptr;
    size_t size = 0;
    auto fileRead = reinterpret_cast<FileRead_t>(Offsets::FUN_FILE_READ);
    if (fileRead(0, path, &buf, &size, 1, 1, 0) == 0 || buf == nullptr)
        return false;

    const char *c = static_cast<const char *>(buf);
    static const char kKey[] = "## AllowAddOnTableAccess:";
    constexpr size_t kKeyLen = sizeof(kKey) - 1;

    bool allowed = false;
    for (size_t i = 0; i + kKeyLen <= size; ++i) {
        const bool atLineStart = (i == 0) || c[i - 1] == '\n';
        if (!atLineStart || !EqualsCI(c + i, kKey, kKeyLen))
            continue;
        const char *p = c + i + kKeyLen;
        const char *end = c + size;
        while (p < end && (*p == ' ' || *p == '\t'))
            ++p;
        int value = 0;
        bool anyDigit = false;
        while (p < end && *p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            ++p;
            anyDigit = true;
        }
        allowed = anyDigit && value != 0;
        break;
    }

    auto smemFree = reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE);
    smemFree(buf, __FILE__, __LINE__, 0);
    return allowed;
}

// `C_AddOns.GetAddOnLocalTable(name)` -> table, or nil.
//
// Name (string) input only — the local table is a by-name concept (one addon
// asking for another by name), and the namespace map is name-keyed. Numeric
// index is not accepted (returns nil); no addon references another's table by
// load order.
int __fastcall Script_GetAddOnLocalTable(void *L) {
    const char *name = Game::Lua::ToString(L, 1);
    if (name == nullptr || *name == '\0') {
        Game::Lua::PushNil(L);
        return 1;
    }

    auto isLoaded = reinterpret_cast<IsLoadedFn_t>(Offsets::FUN_ADDON_IS_LOADED);
    if (isLoaded(name) == 0) { // not loaded (covers "does not exist")
        Game::Lua::PushNil(L);
        return 1;
    }

    if (!TocAllowsTableAccess(name)) { // opted out / no directive
        Game::Lua::PushNil(L);
        return 1;
    }

    LuaSyntax::PushAddonNamespace(L, name);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnLocalTable",
                                     &Script_GetAddOnLocalTable);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AddOns::LocalTable
