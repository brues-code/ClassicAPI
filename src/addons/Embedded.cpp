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

// Embedded addons. When the user doesn't have an addon installed on disk,
// this module makes the engine think they do — without writing anything
// to the filesystem. The addons are `Manifest::kAddons` (the `ADDONS`
// list in CMakeLists.txt): `!!!ClassicAPI`, which loads at login, and
// `ClassicAPI_DebugTools`, which is `## LoadOnDemand` behind the
// `/dump` / `/fstack` / `/etrace` stubs in `!!!ClassicAPI` — retail's
// chat-frame + Blizzard_DebugTools split. (The `Blizzard_` name is not
// available to us: the engine moves loose `Interface\AddOns\Blizzard*`
// folders aside at startup, `MoveAsideLooseInterface`, and refuses a
// `Blizzard_*` TOC without a signature.)
//
// How:
//
//   1. The CMake build embeds every file of every listed addon as an
//      RCDATA resource, plus a generated `{path, resourceId}` manifest
//      (`embedded_resources.h`).
//
//   2. We hook the engine's file-read function at `FUN_FILE_READ`.
//      When the engine asks for a path under
//      `Interface\AddOns\<embedded addon>\`, we allocate a Storm buffer
//      via the same allocator the engine itself uses, copy the
//      embedded content in, and hand it back. The caller's normal
//      `SMemFree` reclaims it on completion — no special lifetime.
//
//   3. We hook the exists-check `FUN_FILE_EXISTS` so it answers the same
//      way for the same paths. Without it every embedded file reads fine
//      but reports as missing — the per-addon loader would skip an
//      embedded `Bindings.xml`, and `Xml::ScriptPath` would never find an
//      embedded `<Script file="sub\x.lua">`.
//
//   4. We pre-hook the engine's `FUN_ADDON_SCAN_FULL` and call the TOC
//      parser with each embedded name before the scan runs. The registry
//      is empty there (the login init resets it just before), so our
//      entries are the first records and the load pass — which walks the
//      records in registration order — loads `!!!ClassicAPI` first. The
//      scan's own `## LoadWith:` pass then sees them like any disk addon.
//      The parser is dedup-safe: when the user also has an addon on disk,
//      the scan's call for that folder hits the hash table and is a no-op.
//
// Every embedded addon is hidden from the addon list and force-enabled,
// the way Blizzard's own addons are always available and never listed.
//
// Same pattern WeirdUtils.dll uses (we reverse-engineered it from
// `0x10015A74` while figuring this out). The addon load pass then
// reads each `.lua` file through the same hooked read path —
// `Compat.lua`, `Util\Print.lua`, etc. all surface from
// embedded buffers.

#include "embedded_resources.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/Embedded.h"
#include "addons/EngineIO.h"
#include "addons/FlavorBindings.h"
#include "addons/FlavorToc.h"
#include "addons/Registry.h"
#include "addons/Toc.h"
#include "addons/TocRewrite.h"
#include "bindings/Inject.h"
#include "embed/Resource.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Addons::Embedded {

namespace {

using Manifest::kAddonCount;
using Manifest::kAddons;

// Developer marker. When this file exists in an on-disk addon folder,
// the disk copy of that addon wins unconditionally (see
// `DiskHasDevMarker` / `DecideSource`). It is gitignored, excluded from
// the embed, and the release ships only the DLL — so it can only exist
// where a developer deliberately created it, never in a normal user's
// install.
constexpr const char *kDevMarkerFile = ".classicapi-dev";

// `Interface\AddOns\` — the prefix the engine constructs for any addon
// file; the manifest paths are relative to it. Comparing case-
// insensitively because the TOC parser uses `Interface\AddOns\%s\…`
// where %s comes from the on-disk directory name, and Windows file
// paths are case-insensitive.
constexpr const char *kAddOnsPrefix = "Interface\\AddOns\\";

char NormalizeChar(char c) {
    if (c == '/') return '\\';
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c + 32);
    return c;
}

bool PathEqualsCI(const char *a, const char *b) {
    while (*a && *b) {
        if (NormalizeChar(*a) != NormalizeChar(*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// `path` with the case-insensitive `prefix` stripped, NULL when it does
// not start with it.
const char *StripPrefixCI(const char *path, const char *prefix) {
    while (*prefix) {
        if (*path == '\0') return nullptr;
        if (NormalizeChar(*path) != NormalizeChar(*prefix)) return nullptr;
        ++path; ++prefix;
    }
    return path;
}

int IndexOfAddon(const char *name) {
    for (size_t i = 0; i < kAddonCount; ++i) {
        if (PathEqualsCI(name, kAddons[i]))
            return static_cast<int>(i);
    }
    return -1;
}

// The embedded addon `path` lies in (`Interface\AddOns\<addon>\…`), or -1.
// `*relPath` gets the manifest-relative part (`<addon>\Util\Print.lua`).
int MatchAddonPath(const char *path, const char **relPath) {
    if (path == nullptr) return -1;
    const char *rel = StripPrefixCI(path, kAddOnsPrefix);
    if (rel == nullptr) return -1;
    for (size_t i = 0; i < kAddonCount; ++i) {
        const char *rest = StripPrefixCI(rel, kAddons[i]);
        if (rest != nullptr && NormalizeChar(*rest) == '\\') {
            *relPath = rel;
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The embedded bytes for manifest path `relPath`; `data` is NULL when it
// is not embedded.
Embed::Bytes LookupEmbedded(const char *relPath) {
    for (size_t i = 0; i < Manifest::kFileCount; ++i) {
        if (PathEqualsCI(relPath, Manifest::kFiles[i].path))
            return Embed::Resource(Manifest::kFiles[i].resourceId);
    }
    return {nullptr, 0};
}

// Engine file I/O + Storm allocator — see addons/EngineIO.h for the shapes and
// the __stdcall / ESP-drift hazard. A buffer we SMemAlloc here is freed cleanly
// by the caller's standard SMemFree in turn.
using AddOns::EngineIO::FileExistsFn;
using AddOns::EngineIO::FileReadFn;
using AddOns::EngineIO::SMemAllocFn;
using AddOns::EngineIO::SMemFreeFn;

// The trampolines to the original FUN_FILE_READ / FUN_FILE_EXISTS (installed
// by the co-hooks below).
FileReadFn FileRead_o = nullptr;
FileExistsFn FileExists_o = nullptr;

// Extracts the value of the `## Version: X` line from a TOC byte
// buffer. Writes into `out` (size `outSize`) and returns true on
// success. The TOC format is `## Key: Value`, one per line, case-
// sensitive on the key. Returns false if no Version line is found.
bool ExtractTocVersion(const char *content, size_t size,
                       char *out, size_t outSize) {
    const char *v = nullptr;
    size_t n = 0;
    if (outSize == 0)
        return false;
    if (!AddOns::Toc::FindValue(content, size, "Version", &v, &n) || n == 0) {
        out[0] = '\0';
        return false;
    }
    if (n >= outSize)
        n = outSize - 1;
    std::memcpy(out, v, n);
    out[n] = '\0';
    return true;
}

// Returns -1/0/+1 for `a < b` / `a == b` / `a > b`. "DEV" is the
// local-build sentinel: two DEV builds are equal, but a DEV build sorts
// BELOW any real release. Dev-over-release precedence is signalled
// explicitly by the `.classicapi-dev` marker now — NOT by the toc
// version — so a stray `## Version: DEV` disk copy (repo clone / old
// bundle) never silently shadows a newer embedded release. Otherwise
// the two strings are walked as dot-separated numeric semver components
// (`1.2` < `1.10`).
int CompareVersions(const char *a, const char *b) {
    const bool aDev = std::strcmp(a, "DEV") == 0;
    const bool bDev = std::strcmp(b, "DEV") == 0;
    if (aDev && bDev) return 0;
    if (aDev) return -1;
    if (bDev) return 1;
    while (*a || *b) {
        int va = 0, vb = 0;
        while (*a >= '0' && *a <= '9') { va = va * 10 + (*a - '0'); ++a; }
        while (*b >= '0' && *b <= '9') { vb = vb * 10 + (*b - '0'); ++b; }
        if (va != vb) return va < vb ? -1 : 1;
        if (*a == '.') ++a;
        if (*b == '.') ++b;
        if (!*a && !*b) break;
        if (!*a) return -1;
        if (!*b) return 1;
    }
    return 0;
}

// Which source wins the embed-vs-disk comparison, decided per addon once
// on its first file read.
enum class Source { Undecided, Disk, Embedded };
Source g_source[kAddonCount] = {};

// True iff addon `addon`'s on-disk folder contains the `.classicapi-dev`
// marker. Asked of the ORIGINAL `FUN_FILE_EXISTS` (bypassing our embed
// hook), so it reflects a real file on disk / in an MPQ — never an
// embedded copy. That makes it a clean "is this a developer working
// tree" signal: the marker is gitignored + excluded from the embed and
// the release ships only the DLL, so a normal user's install can't
// carry it.
bool DiskHasDevMarker(int addon) {
    char fullPath[256];
    std::snprintf(fullPath, sizeof(fullPath), "%s%s\\%s", kAddOnsPrefix,
                  kAddons[addon], kDevMarkerFile);
    return FileExists_o(fullPath, 1) != 0;
}

// Decide whether disk or embedded should serve all reads for addon
// `addon`. Called on the first FileRead_h / FileExists_h call under its
// folder. The `.classicapi-dev` marker forces disk unconditionally
// (explicit developer override). Otherwise reads the on-disk TOC via the
// original `FUN_FILE_READ` (bypassing our hook), parses its version,
// compares to the embedded version, and caches the newer. If disk has no
// TOC at all, embedded wins by default.
void DecideSource(int addon) {
    if (g_source[addon] != Source::Undecided) return;

    // Explicit developer override: disk wins regardless of version. This
    // is how a dev keeps disk precedence when running local Lua against a
    // *released* DLL (where the embedded version would otherwise be newer
    // and win). Dev intent lives in this marker, decoupled from the toc
    // version — so users never get silently shadowed by a stray copy.
    if (DiskHasDevMarker(addon)) {
        g_source[addon] = Source::Disk;
        return;
    }

    char embeddedVersion[64] = "";
    char tocRelPath[256];
    std::snprintf(tocRelPath, sizeof(tocRelPath), "%s\\%s.toc",
                  kAddons[addon], kAddons[addon]);
    const Embed::Bytes toc = LookupEmbedded(tocRelPath);
    if (toc.data != nullptr)
        ExtractTocVersion(reinterpret_cast<const char *>(toc.data), toc.size,
                          embeddedVersion, sizeof(embeddedVersion));

    char fullPath[256];
    std::snprintf(fullPath, sizeof(fullPath), "%s%s", kAddOnsPrefix,
                  tocRelPath);

    void *diskBuf = nullptr;
    size_t diskSize = 0;
    const int ok = FileRead_o(0, fullPath, &diskBuf, &diskSize, 1, 1, 0);
    if (ok == 0 || diskBuf == nullptr) {
        g_source[addon] = Source::Embedded;
        return;
    }

    char diskVersion[64] = "";
    ExtractTocVersion(static_cast<const char *>(diskBuf), diskSize,
                      diskVersion, sizeof(diskVersion));

    // Free the disk buffer we just borrowed — we only needed it for
    // the version-line scrape. The actual content for the engine's
    // TOC read comes through the regular hook path below.
    auto SMemFree = reinterpret_cast<SMemFreeFn>(Offsets::FUN_STORM_SMEM_FREE);
    SMemFree(diskBuf, __FILE__, __LINE__, 0);

    // Missing or unparseable disk version → assume it's older than
    // anything we ship, embedded wins.
    if (diskVersion[0] == '\0') {
        g_source[addon] = Source::Embedded;
        return;
    }

    const int cmp = CompareVersions(embeddedVersion, diskVersion);
    g_source[addon] = (cmp > 0) ? Source::Embedded : Source::Disk;
}

int __stdcall FileRead_h(int unused, const char *path, void **outBuf,
                         size_t *outSize, size_t extraBytes,
                         int flag1, int flag2) {
    // FrameXML Bindings.xml gets an inline splice for our TARGETING
    // additions — see `bindings/Inject.cpp`. Returns true with the
    // patched buffer ready for the engine's binding parser to consume.
    if (Bindings::Inject::TryHandle(unused, path, outBuf, outSize,
                                    extraBytes, flag1, flag2, FileRead_o)) {
        return 1;
    }

    const char *relPath = nullptr;
    const int addon = MatchAddonPath(path, &relPath);
    if (addon < 0) {
        // Not an embedded addon. If this is a base-TOC read for a multi-flavor
        // addon that ships only `<Name>_Turtle.toc` / `_ClassicAPI.toc`, serve
        // that in place of the missing `<Name>.toc` so it registers and loads
        // (see addons/FlavorToc.h).
        int result;
        if (AddOns::FlavorToc::TryHandle(unused, path, outBuf, outSize,
                                         extraBytes, flag1, flag2, FileRead_o)) {
            result = 1;
        } else if (AddOns::FlavorBindings::TryHandle(unused, path, outBuf, outSize,
                                                     extraBytes, flag1, flag2,
                                                     FileRead_o)) {
            // Flavor-specific addon keybindings: serve `Bindings_Turtle.xml` /
            // `Bindings_ClassicAPI.xml` for a `…\AddOns\<Name>\Bindings.xml`
            // read (see addons/FlavorBindings.h). The paired exists-check
            // co-hook lives in that module.
            result = 1;
        } else {
            result = FileRead_o(unused, path, outBuf, outSize, extraBytes,
                                flag1, flag2);
        }
        // Per-line TOC directives: evaluate `[Condition]` and expand
        // `[Variable]` tokens on this addon TOC's file lines so the load
        // pass sees clean paths (see addons/TocRewrite.h). No-op unless
        // `path` is an addon `.toc` that actually contains directives.
        if (result != 0)
            AddOns::TocRewrite::Transform(path, outBuf, outSize);
        return result;
    }

    DecideSource(addon);

    if (g_source[addon] == Source::Disk) {
        // Disk version is at least as new — serve disk. If disk
        // doesn't have this specific file (e.g. embedded has files
        // disk doesn't), fall through and try embedded.
        const int diskResult = FileRead_o(unused, path, outBuf, outSize,
                                           extraBytes, flag1, flag2);
        if (diskResult != 0) return diskResult;
    }

    // Source::Embedded path, OR Source::Disk-but-file-missing fallback.
    // Look up the embedded version and synthesize a Storm buffer the
    // caller can free normally.
    const Embed::Bytes entry = LookupEmbedded(relPath);
    if (entry.data == nullptr) {
        // Not in our embedded set either — let the engine try disk
        // one more time (returns 0 if that also fails). Covers
        // pathological cases like an empty disk install + embedded
        // missing some file.
        return FileRead_o(unused, path, outBuf, outSize, extraBytes,
                          flag1, flag2);
    }

    auto SMemAlloc = reinterpret_cast<SMemAllocFn>(
        Offsets::FUN_STORM_SMEM_ALLOC);
    const size_t totalSize = entry.size + extraBytes;
    void *buf = SMemAlloc(totalSize, __FILE__, __LINE__, 0);
    if (buf == nullptr) return 0;
    std::memcpy(buf, entry.data, entry.size);
    if (extraBytes > 0) {
        std::memset(static_cast<uint8_t *>(buf) + entry.size, 0, extraBytes);
    }
    if (outBuf != nullptr) *outBuf = buf;
    if (outSize != nullptr) *outSize = entry.size;
    return 1;
}

// The exists-check counterpart of FileRead_h: the same source decision and
// the same fallbacks, so a path reports as existing exactly when FileRead_h
// would serve it. Other paths go to FlavorBindings (flavor-only addon
// Bindings.xml), then to the original.
int __stdcall FileExists_h(const char *path, int mode) {
    const char *relPath = nullptr;
    const int addon = MatchAddonPath(path, &relPath);
    if (addon < 0) {
        if (FileExists_o(path, mode) != 0)
            return 1;
        return AddOns::FlavorBindings::TryExists(path, mode, FileExists_o) ? 1 : 0;
    }

    DecideSource(addon);
    if (g_source[addon] == Source::Disk && FileExists_o(path, mode) != 0)
        return 1;
    if (LookupEmbedded(relPath).data != nullptr)
        return 1;
    return FileExists_o(path, mode);
}

// `FUN_ADDON_SCAN_FULL` — `void __cdecl()`. We pre-hook it: register the
// embedded addons into the empty registry, then let the engine's scan
// run. Dedup-safe — see file header.
using AddonScan_t = void(__cdecl *)();
AddonScan_t AddonScan_o = nullptr;

using TocParser_t = void(__fastcall *)(const char *name);

// `entry + OFF_ADDON_ENTRY_FILTER_OUT` is the "filter-out" byte. The
// flat display-array builder `FUN_0051da70` walks the linked list and
// copies an entry into `GetNumAddOns`/`GetAddOnInfo`'s array ONLY when
// this byte is 0, so setting it hides an embedded addon from the
// character-select AddOns list — there's no checkbox, so it can't be
// toggled off. The builder only ever *writes* this byte on the
// secure/SMSG path (`entry+0x28 != 0`); our entry is non-secure, so a
// manual `1` here is stable across rebuilds. Hiding does NOT stop
// loading (the load pass `FUN_0051f600` never reads `+0x29`) and does
// NOT break dependency resolution or by-name queries (those use the
// addon hash table, which keeps the entry regardless — `LoadAddOn` of a
// hidden load-on-demand entry still works).
//
// DefaultState (`+0x2b`) needs no write: the entry constructor
// `FUN_00520550` stores 1 there, our tocs have no `## DefaultState:`, and
// the resolver co-hook below forces enabled anyway.

// Post-registration finalize for the embedded entries: hide them from the
// addon list. Combined with the `FUN_ADDON_ENABLE_RESOLVE` co-hook, an
// embedded addon is always loadable and never appears as a toggleable
// entry.
void FinalizeEmbeddedEntries() {
    Addons::ForEachEntry([](uintptr_t entry) {
        const char *name = *reinterpret_cast<const char *const *>(
            entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
        if (name != nullptr && IndexOfAddon(name) >= 0)
            *reinterpret_cast<uint8_t *>(
                entry + Offsets::OFF_ADDON_ENTRY_FILTER_OUT) = 1;
    });
}

void __cdecl AddonScan_h() {
    auto TocParser = reinterpret_cast<TocParser_t>(Offsets::FUN_TOC_PARSER);
    for (size_t i = 0; i < kAddonCount; ++i)
        TocParser(kAddons[i]);
    FinalizeEmbeddedEntries();
    AddonScan_o();
}

// `FUN_ADDON_ENABLE_RESOLVE` — per-character enable resolver. We
// co-hook it to force-enable the embedded addons regardless of
// DefaultState or any stale WTF disable-override, so hiding them from
// the addon list can never leave one stuck off. Returns 2 ("enabled for
// all"); the load-pass gate only tests `!= 0`. Every other addon falls
// through to the original untouched. See the Offsets.h note for the ABI.
using AddonEnableResolve_t =
    uint32_t(__fastcall *)(const char *name, const char *account, char resolveAll);
AddonEnableResolve_t AddonEnableResolve_o = nullptr;

uint32_t __fastcall AddonEnableResolve_h(const char *name, const char *account,
                                         char resolveAll) {
    if (name != nullptr && IndexOfAddon(name) >= 0)
        return 2;
    return AddonEnableResolve_o(name, account, resolveAll);
}

const Game::HookAutoRegister _hookFileRead{
    Offsets::FUN_FILE_READ,
    reinterpret_cast<void *>(&FileRead_h),
    reinterpret_cast<void **>(&FileRead_o)};

const Game::HookAutoRegister _hookFileExists{
    Offsets::FUN_FILE_EXISTS,
    reinterpret_cast<void *>(&FileExists_h),
    reinterpret_cast<void **>(&FileExists_o)};

const Game::HookAutoRegister _hookAddonScan{
    Offsets::FUN_ADDON_SCAN_FULL,
    reinterpret_cast<void *>(&AddonScan_h),
    reinterpret_cast<void **>(&AddonScan_o)};

const Game::HookAutoRegister _hookEnableResolve{
    Offsets::FUN_ADDON_ENABLE_RESOLVE,
    reinterpret_cast<void *>(&AddonEnableResolve_h),
    reinterpret_cast<void **>(&AddonEnableResolve_o)};

} // namespace

bool IsEmbedded(const char *name) {
    return name != nullptr && IndexOfAddon(name) >= 0;
}

} // namespace Addons::Embedded
