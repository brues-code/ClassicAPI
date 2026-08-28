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

// Embedded `!!!ClassicAPI` addon fallback. When the user doesn't have
// the addon installed on disk, this module makes the engine think it
// does — without writing anything to the filesystem.
//
// How:
//
//   1. The CMake build embeds every file under `AddOns/!!!ClassicAPI/`
//      into a generated header (`embedded_classicapi.h`) as a byte
//      array per file plus a `{path, data, size}` manifest.
//
//   2. We hook the engine's file-read function at `FUN_FILE_READ`.
//      When the engine asks for a path under
//      `Interface\AddOns\!!!ClassicAPI\`, we allocate a Storm buffer
//      via the same allocator the engine itself uses, copy the
//      embedded content in, and hand it back. The caller's normal
//      `SMemFree` reclaims it on completion — no special lifetime.
//
//   3. We post-hook the engine's `FUN_ADDON_INIT`. After the engine's
//      own `Interface\AddOns\` scan finishes, we call the TOC parser
//      directly with `"!!!ClassicAPI"`. The parser opens its TOC via
//      our hooked read function and registers the addon as a normal
//      entry. The TOC parser is dedup-safe: if the user already has
//      the addon on disk, the engine's scan picked it up, the
//      hash-table lookup hits, and our call is a no-op. So the
//      conditional "only if not on disk" behavior is free.
//
// Same pattern WeirdUtils.dll uses (we reverse-engineered it from
// `0x10015A74` while figuring this out). The addon load pass then
// reads each `.lua` file through the same hooked read path —
// `Compat.lua`, `Util\Print.lua`, etc. all surface from
// embedded buffers.

#include "embedded_classicapi.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"
#include "addons/FlavorBindings.h"
#include "addons/FlavorToc.h"
#include "addons/Registry.h"
#include "addons/Toc.h"
#include "addons/TocRewrite.h"
#include "bindings/Inject.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Addons::Embedded {

namespace {

constexpr const char *kAddonName = "!!!ClassicAPI";
constexpr const char *kAddonTocFile = "!!!ClassicAPI.toc";

// Developer marker. When this file exists in the on-disk addon folder,
// the disk copy wins unconditionally (see `DiskHasDevMarker` /
// `DecideSource`). It is gitignored, excluded from the embed, and the
// release ships only the DLL — so it can only exist where a developer
// deliberately created it, never in a normal user's install.
constexpr const char *kDevMarkerFile = ".classicapi-dev";

// `Interface\AddOns\!!!ClassicAPI\` — the prefix the engine
// constructs for any of our addon's files. Comparing case-
// insensitively because the TOC parser uses `Interface\AddOns\%s\…`
// where %s comes from the on-disk directory name, and Windows file
// paths are case-insensitive.
constexpr const char *kAddonPathPrefix = "Interface\\AddOns\\!!!ClassicAPI\\";
constexpr size_t kAddonPathPrefixLen = 30; // length of the literal above

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

// Strip the `Interface\AddOns\!!!ClassicAPI\` prefix and return the
// suffix (e.g. `Util\Print.lua`) on a match, NULL otherwise.
const char *StripAddonPrefix(const char *path) {
    if (path == nullptr) return nullptr;
    const char *p = path;
    const char *q = kAddonPathPrefix;
    while (*q) {
        if (*p == '\0') return nullptr;
        if (NormalizeChar(*p) != NormalizeChar(*q)) return nullptr;
        ++p; ++q;
    }
    return p;
}

const ClassicAPIFiles::File *LookupEmbedded(const char *suffix) {
    for (size_t i = 0; i < ClassicAPIFiles::kFileCount; ++i) {
        if (PathEqualsCI(suffix, ClassicAPIFiles::kFiles[i].path))
            return &ClassicAPIFiles::kFiles[i];
    }
    return nullptr;
}

// Engine file I/O + Storm allocator — see addons/EngineIO.h for the shapes and
// the __stdcall / ESP-drift hazard. A buffer we SMemAlloc here is freed cleanly
// by the caller's standard SMemFree in turn.
using AddOns::EngineIO::FileReadFn;
using AddOns::EngineIO::SMemAllocFn;
using AddOns::EngineIO::SMemFreeFn;

// The trampoline to the original FUN_FILE_READ (installed by the co-hook below).
FileReadFn FileRead_o = nullptr;

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
    if (!AddOns::Toc::FindValue(content, size, "## Version:", &v, &n) || n == 0) {
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

// Pre-extracted embedded TOC version, populated lazily on first
// FileRead_h call under our prefix. Empty until populated.
char g_embeddedVersion[64] = "";

// Which source wins the embed-vs-disk comparison, decided once on
// first relevant file read.
enum class Source { Undecided, Disk, Embedded };
Source g_source = Source::Undecided;

void EnsureEmbeddedVersionExtracted() {
    if (g_embeddedVersion[0] != '\0') return;
    for (size_t i = 0; i < ClassicAPIFiles::kFileCount; ++i) {
        if (PathEqualsCI(kAddonTocFile, ClassicAPIFiles::kFiles[i].path)) {
            ExtractTocVersion(
                reinterpret_cast<const char *>(ClassicAPIFiles::kFiles[i].data),
                ClassicAPIFiles::kFiles[i].size,
                g_embeddedVersion, sizeof(g_embeddedVersion));
            return;
        }
    }
}

// True iff the on-disk addon folder contains the `.classicapi-dev`
// marker. Read via the ORIGINAL `FUN_FILE_READ` (bypassing our embed
// hook), so it reflects a real file on disk / in an MPQ — never an
// embedded copy. That makes it a clean "is this a developer working
// tree" signal: the marker is gitignored + excluded from the embed and
// the release ships only the DLL, so a normal user's install can't
// carry it.
bool DiskHasDevMarker() {
    char fullPath[256];
    std::snprintf(fullPath, sizeof(fullPath), "%s%s",
                  kAddonPathPrefix, kDevMarkerFile);
    void *buf = nullptr;
    size_t size = 0;
    const int ok = FileRead_o(0, fullPath, &buf, &size, 1, 1, 0);
    if (ok == 0 || buf == nullptr)
        return false;
    auto SMemFree = reinterpret_cast<SMemFreeFn>(Offsets::FUN_STORM_SMEM_FREE);
    SMemFree(buf, __FILE__, __LINE__, 0);
    return true;
}

// Decide whether disk or embedded should serve all reads for this
// addon. Called on the first FileRead_h call with a matching prefix.
// The `.classicapi-dev` marker forces disk unconditionally (explicit
// developer override). Otherwise reads the on-disk TOC via the original
// `FUN_FILE_READ` (bypassing our hook), parses its version, compares to
// the embedded version, and caches the newer. If disk has no TOC at
// all, embedded wins by default.
void DecideSource() {
    if (g_source != Source::Undecided) return;
    EnsureEmbeddedVersionExtracted();

    // Explicit developer override: disk wins regardless of version. This
    // is how a dev keeps disk precedence when running local Lua against a
    // *released* DLL (where the embedded version would otherwise be newer
    // and win). Dev intent lives in this marker, decoupled from the toc
    // version — so users never get silently shadowed by a stray copy.
    if (DiskHasDevMarker()) {
        g_source = Source::Disk;
        return;
    }

    char fullPath[256];
    std::snprintf(fullPath, sizeof(fullPath), "%s%s",
                  kAddonPathPrefix, kAddonTocFile);

    void *diskBuf = nullptr;
    size_t diskSize = 0;
    const int ok = FileRead_o(0, fullPath, &diskBuf, &diskSize, 1, 1, 0);
    if (ok == 0 || diskBuf == nullptr) {
        g_source = Source::Embedded;
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
        g_source = Source::Embedded;
        return;
    }

    const int cmp = CompareVersions(g_embeddedVersion, diskVersion);
    g_source = (cmp > 0) ? Source::Embedded : Source::Disk;
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

    const char *suffix = StripAddonPrefix(path);
    if (suffix == nullptr) {
        // Not our embedded addon. If this is a base-TOC read for a multi-flavor
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

    DecideSource();

    if (g_source == Source::Disk) {
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
    const auto *entry = LookupEmbedded(suffix);
    if (entry == nullptr) {
        // Not in our embedded set either — let the engine try disk
        // one more time (returns 0 if that also fails). Covers
        // pathological cases like an empty disk install + embedded
        // missing some file.
        return FileRead_o(unused, path, outBuf, outSize, extraBytes,
                          flag1, flag2);
    }

    auto SMemAlloc = reinterpret_cast<SMemAllocFn>(
        Offsets::FUN_STORM_SMEM_ALLOC);
    const size_t totalSize = entry->size + extraBytes;
    void *buf = SMemAlloc(totalSize, __FILE__, __LINE__, 0);
    if (buf == nullptr) return 0;
    std::memcpy(buf, entry->data, entry->size);
    if (extraBytes > 0) {
        std::memset(static_cast<uint8_t *>(buf) + entry->size, 0, extraBytes);
    }
    if (outBuf != nullptr) *outBuf = buf;
    if (outSize != nullptr) *outSize = entry->size;
    return 1;
}

// `FUN_ADDON_INIT` — `__fastcall(accountName)`. We post-hook it: let
// the engine's normal `Interface\AddOns\` scan run first, then call
// the TOC parser with `"!!!ClassicAPI"`. Dedup-safe — see file header.
using AddonInit_t = void(__fastcall *)(const char *accountName);
AddonInit_t AddonInit_o = nullptr;

using TocParser_t = void(__fastcall *)(const char *name);
using ListInsert_t = void(__thiscall *)(void *listCtrl, void *entry,
                                         int position, int anchor);

// `FUN_TOC_PARSER` appends new entries to the *tail* of the addon
// registry's linked list, and the engine's addon-load pass at
// `FUN_0051F600` walks that list in *insertion order* (NOT
// alphabetical order — the alphabetical sort only applies to the
// flat array `GetAddOnInfo(i)` reads from). So our post-hook
// injection lands at the very end of the load order, after every
// disk-scanned addon. Addons that consume our globals from their
// main chunk (idTip's `FrameWatcher`, etc.) would see `nil` and
// hard-error during their boot.
//
// Fix: walk the linked list, find our entry by name, and re-link
// it to the head via `FUN_INTRUSIVE_LIST_INSERT(position=1)`. The
// engine's own insert helper handles remove-then-insert atomically,
// so it's safe to call on an entry that's already in the list.
//
// The list is walked via the shared `Addons::ForEachEntry`
// (`addons/Registry.h`); the callback stops the walk once it finds and
// re-links our entry, since the re-link invalidates the current
// next-pointer.
//
// `entry + OFF_ADDON_ENTRY_FILTER_OUT` is the "filter-out" byte. The
// flat display-array builder `FUN_0051da70` walks the linked list and
// copies an entry into `GetNumAddOns`/`GetAddOnInfo`'s array ONLY when
// this byte is 0, so setting it hides `!!!ClassicAPI` from the
// character-select AddOns list — there's no checkbox, so it can't be
// toggled off. The builder only ever *writes* this byte on the
// secure/SMSG path (`entry+0x28 != 0`); our entry is non-secure, so a
// manual `1` here is stable across rebuilds. Hiding does NOT stop
// loading (the load pass `FUN_0051f600` never reads `+0x29`) and does
// NOT break dependency resolution or by-name queries (those use the
// addon hash table, which keeps the entry regardless).

// `entry+0x2b` — DefaultState (`## DefaultState: enabled/disabled`).
// The enable resolver `FUN_ADDON_ENABLE_RESOLVE` falls back to this
// when the player has no per-character override. Belt-and-suspenders
// alongside the resolver co-hook below (which forces enabled anyway).
constexpr int OFF_ADDON_ENTRY_DEFAULT_STATE = 0x2b;

// Post-registration finalize for the embedded entry: move it to the
// head of the load-order list (so consumers that read our globals from
// their main chunk see them — see the block comment above the offsets),
// hide it from the addon list, and mark it default-enabled. Combined
// with the `FUN_ADDON_ENABLE_RESOLVE` co-hook, `!!!ClassicAPI` always
// loads and never appears as a toggleable entry.
void FinalizeEmbeddedEntry() {
    Addons::ForEachEntry([](uintptr_t entry) -> bool {
        const char *name = *reinterpret_cast<const char *const *>(
            entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
        if (name == nullptr || std::strcmp(name, kAddonName) != 0)
            return true; // keep walking

        // Hide from the character-select AddOns list and default it to
        // enabled, then move it to the head of the load order.
        *reinterpret_cast<uint8_t *>(
            entry + Offsets::OFF_ADDON_ENTRY_FILTER_OUT) = 1;
        *reinterpret_cast<uint8_t *>(entry + OFF_ADDON_ENTRY_DEFAULT_STATE) = 1;

        auto fn = reinterpret_cast<ListInsert_t>(
            Offsets::FUN_INTRUSIVE_LIST_INSERT);
        fn(reinterpret_cast<void *>(
               static_cast<uintptr_t>(Offsets::VAR_ADDON_LIST_CTRL)),
           reinterpret_cast<void *>(entry),
           /*position=*/ 1, /*anchor=*/ 0);
        return false; // stop — the re-link invalidated our next-pointer
    });
}

void __fastcall AddonInit_h(const char *accountName) {
    AddonInit_o(accountName);
    auto TocParser = reinterpret_cast<TocParser_t>(Offsets::FUN_TOC_PARSER);
    TocParser(kAddonName);
    FinalizeEmbeddedEntry();
}

// `FUN_ADDON_ENABLE_RESOLVE` — per-character enable resolver. We
// co-hook it to force-enable `!!!ClassicAPI` regardless of DefaultState
// or any stale WTF disable-override, so hiding it from the addon list
// can never leave it stuck off. Returns 2 ("enabled for all"); the
// load-pass gate only tests `!= 0`. Every other addon falls through to
// the original untouched. See the Offsets.h note for the ABI.
using AddonEnableResolve_t =
    uint32_t(__fastcall *)(const char *name, const char *account, char resolveAll);
AddonEnableResolve_t AddonEnableResolve_o = nullptr;

uint32_t __fastcall AddonEnableResolve_h(const char *name, const char *account,
                                         char resolveAll) {
    if (name != nullptr && PathEqualsCI(name, kAddonName))
        return 2;
    return AddonEnableResolve_o(name, account, resolveAll);
}

const Game::HookAutoRegister _hookFileRead{
    Offsets::FUN_FILE_READ,
    reinterpret_cast<void *>(&FileRead_h),
    reinterpret_cast<void **>(&FileRead_o)};

const Game::HookAutoRegister _hookAddonInit{
    Offsets::FUN_ADDON_INIT,
    reinterpret_cast<void *>(&AddonInit_h),
    reinterpret_cast<void **>(&AddonInit_o)};

const Game::HookAutoRegister _hookEnableResolve{
    Offsets::FUN_ADDON_ENABLE_RESOLVE,
    reinterpret_cast<void *>(&AddonEnableResolve_h),
    reinterpret_cast<void **>(&AddonEnableResolve_o)};

} // namespace

} // namespace Addons::Embedded
