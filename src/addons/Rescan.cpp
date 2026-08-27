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

// Retail-like `/reload`: pick up new addon folders and new files without
// restarting the client. Two independent engine limitations block that on
// a stock 1.12 client, each fixed here with the engine's own machinery:
//
//   1. FILE VISIBILITY. Every relative-path read resolves through a
//      loose-file hash index built ONCE at boot (see Offsets.h,
//      `FUN_VFS_INDEX_SUBTREE`); files created after boot — a new
//      addon's TOC, a new .lua in an existing addon, a freshly written
//      SavedVariables file — are invisible until restart. The boot
//      indexer is dedup-safe and works on subtrees, so we re-run it per
//      /reload on the only two subtrees whose contents can change what
//      a /reload loads: `Interface\AddOns` and `WTF\Account`. New files
//      in EXISTING addons need nothing more — the per-addon loader
//      `FUN_0051F240` re-reads the TOC from disk on every load pass, so
//      once the files are visible they load.
//
//   2. REGISTRY MEMBERSHIP. The addon registry is built once at login;
//      new folders are never walked. We replay the login scan's disk
//      walk verbatim (`FUN_ADDON_SCAN_DISK_DIRS` with the engine's own
//      per-directory callback, which feeds the dedup-safe TOC parser),
//      so new folders register as completely normal entries — then
//      mirror the two registry structures the scan's OTHER passes would
//      have filled: the reverse-LoadWith lists (scan tail loop) and the
//      flat `GetNumAddOns` display array (`FUN_0051DA70` phase 2).
//
// Runs from the `ModuleAutoRegister` callback — LoadScriptFunctions
// post-hook, which fires inside FrameXML init (`FUN_0048FBF0`) BEFORE
// the AddOns.txt enable-state re-read, the load-progress count, and the
// addon load pass, on both login and every /reload. The engine then
// loads new entries natively: dep ordering, Bindings.xml, flavor TOCs,
// SavedVariables, ADDON_LOADED, the load-screen "Loading add-on %s".
//
// What this deliberately does NOT do (no safe engine mechanism):
//   - re-parse `##` metadata of already-registered addons (the parser
//     dedup-skips them; the only rebuild primitive is the login
//     teardown, and single-entry eviction dangles other entries'
//     reverse-LoadWith pointers) — re-login covers it;
//   - evict deleted addons (same hazard; the stale entry harmlessly
//     loads nothing once its TOC read fails).
//
// NOTE the append-only discipline. A previous attempt re-invoked the
// login-only teardown+rescan (`FUN_ADDON_INIT`) mid-/reload and
// corrupted the registry (duplicate loads + Lua memory explosion on the
// NEXT login). This module never tears down, never evicts, and never
// mutates existing entries beyond reverse-LoadWith appends identical to
// what a login scan would have produced; new entries come from the same
// parser call `Addons::Embedded` has exercised every login.
//
// Thread note: the loose-file index has no lock, but the engine itself
// builds it lazily on first access and we mutate it only during the
// reload loading screen on the main thread — the same context in which
// the engine does all its own file work.

#include "Game.h"
#include "Offsets.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Addons::Rescan {

namespace {

// ── Engine entry points (all verified by disassembly; see Offsets.h) ──

using FindOpen_t = void *(__stdcall *)(const char *dirPath);
using FindClose_t = void(__stdcall *)(void *findBlock);
using IndexSubtree_t = void(__fastcall *)(const char *basePath,
                                          const char *relSubdir,
                                          void *findHandle);
using ScanDiskDirs_t = int(__fastcall *)(const char *basePath,
                                         const char *pattern, void *callback,
                                         void *userParam, int includeHidden);
using ResolveByName_t = void *(__fastcall *)(const char *name);
using DescGrow_t = void(__thiscall *)(void *desc, uint32_t newCap);
using QuantumCalc_t = uint32_t(__thiscall *)(void *desc, uint32_t needed);
using Qsort_t = void(__cdecl *)(void *base, uint32_t num, uint32_t width,
                                void *compare);

// The engine's ubiquitous growable-array descriptor.
struct Desc {
    uint32_t cap;
    uint32_t count;
    uint32_t *data;
    uint32_t quantum;
};

// Append one dword to a descriptor, mirroring the engine's inline
// grow-and-append (the scan tail loop and `FUN_0051DA70` phase 2 share
// it): round the needed cap up to the quantum — computed by the
// engine's own `FUN_DESC_QUANTUM_CALC` when the desc has none — grow
// via the site's grow instantiation (which reallocs `data` and writes
// `cap`), then `data[count++] = value`.
void DescAppend(Desc *desc, uint32_t value, DescGrow_t grow) {
    uint32_t needed = desc->count + 1;
    if (desc->cap < needed) {
        uint32_t quantum = desc->quantum;
        if (quantum == 0) {
            auto calc =
                reinterpret_cast<QuantumCalc_t>(Offsets::FUN_DESC_QUANTUM_CALC);
            quantum = calc(desc, needed);
        }
        if (needed % quantum != 0)
            needed += quantum - needed % quantum;
        grow(desc, needed);
    }
    desc->data[desc->count++] = value;
}

// Walk the registry's intrusive linked list (same traversal as the
// engine's load pass: next at `entry + [VAR_ADDON_LIST_CTRL] + 4`,
// low-bit-1 or NULL terminates).
template <typename Fn> void ForEachEntry(Fn fn) {
    const int linkOffset = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_LIST_CTRL));
    uintptr_t entry = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_LIST_HEAD));
    while ((entry & 1) == 0 && entry != 0) {
        fn(entry);
        entry = *reinterpret_cast<const uintptr_t *>(entry + linkOffset + 4);
    }
}

// By-name registry lookup via the engine's own hash resolver (returns
// the entry's RequiredDeps desc, so subtract its offset — same recovery
// `Addons::Info` uses).
uintptr_t ResolveEntryByName(const char *name) {
    auto resolve =
        reinterpret_cast<ResolveByName_t>(Offsets::FUN_ADDON_RESOLVE_REQ_DEPS);
    const uintptr_t desc = reinterpret_cast<uintptr_t>(resolve(name));
    return desc != 0 ? desc - Offsets::OFF_ADDON_REQDEPS_DESC : 0;
}

// ── Step 1: loose-file index refresh ──────────────────────────────────

// Register new on-disk files under `relSubdir` with the frozen index,
// exactly as the boot walk would have (the indexer skips every file
// already present). No-op when the directory doesn't exist —
// `FUN_VFS_FIND_OPEN` requires an existing directory and returns NULL.
void ReindexSubtree(const char *base, const char *relSubdir) {
    char dir[260];
    std::snprintf(dir, sizeof(dir), "%s\\%s", base, relSubdir);
    auto findOpen = reinterpret_cast<FindOpen_t>(Offsets::FUN_VFS_FIND_OPEN);
    auto findClose = reinterpret_cast<FindClose_t>(Offsets::FUN_VFS_FIND_CLOSE);
    void *handle = findOpen(dir);
    if (handle == nullptr)
        return;
    auto index =
        reinterpret_cast<IndexSubtree_t>(Offsets::FUN_VFS_INDEX_SUBTREE);
    index(base, relSubdir, handle);
    findClose(handle);
}

void RefreshLooseFileIndex() {
    // The boot walk's base choice (`FUN_00646EA0`): the recorded game
    // dir if it opens as a directory, else ".". Index keys are relative
    // to this, so it must match or the new keys would never be hit.
    auto findOpen = reinterpret_cast<FindOpen_t>(Offsets::FUN_VFS_FIND_OPEN);
    auto findClose = reinterpret_cast<FindClose_t>(Offsets::FUN_VFS_FIND_CLOSE);
    const char *base =
        reinterpret_cast<const char *>(Offsets::VAR_VFS_BASE_PATH);
    void *probe = findOpen(base);
    if (probe != nullptr)
        findClose(probe);
    else
        base = ".";

    // The only subtrees whose post-boot changes affect what a /reload
    // loads: addon content, and the SavedVariables the unload pass
    // wrote moments ago (a first-ever SV file is otherwise invisible —
    // the stock client loses a freshly installed addon's settings on
    // /reload for exactly this reason).
    ReindexSubtree(base, "Interface\\AddOns");
    ReindexSubtree(base, "WTF\\Account");
}

// ── Step 2: registry rescan + mirrored fix-ups ────────────────────────

// The scan tail loop's reverse-LoadWith build, restricted to pairs
// involving a newly registered entry. Login already linked old→old
// pairs; a pair is missing exactly when one side is new (the login
// walk skipped `## LoadWith:` names that didn't resolve, and new
// entries were never walked at all) — so appending just those pairs
// reproduces what a login scan would have built, with no duplicates in
// the engine's no-dedup lists.
void FixupReverseLoadWith(const std::vector<uintptr_t> &added) {
    auto isNew = [&added](uintptr_t e) {
        return std::find(added.begin(), added.end(), e) != added.end();
    };
    auto grow =
        reinterpret_cast<DescGrow_t>(Offsets::FUN_ADDON_REVLOADWITH_GROW);
    ForEachEntry([&](uintptr_t entry) {
        const bool entryIsNew = isNew(entry);
        const uint32_t count = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_ADDON_LOADWITH_COUNT);
        auto names = *reinterpret_cast<const char *const *const *>(
            entry + Offsets::OFF_ADDON_LOADWITH_ARRAY);
        for (uint32_t i = 0; i < count; ++i) {
            const uintptr_t target = ResolveEntryByName(names[i]);
            if (target == 0 || (!entryIsNew && !isNew(target)))
                continue;
            DescAppend(reinterpret_cast<Desc *>(
                           target + Offsets::OFF_ADDON_REVLOADWITH_DESC),
                       static_cast<uint32_t>(entry), grow);
        }
    });
}

// `FUN_0051DA70` phase 2, mirrored (never call that function itself —
// its phase 1 consumes an SMSG_ADDON_INFO packet): rebuild the
// `GetNumAddOns`/`GetAddOnInfo(i)` name-pointer array from the linked
// list, skipping filtered entries (keeps `!!!ClassicAPI` hidden), then
// sort with the engine's own comparator to restore alphabetical order.
void RebuildDisplayArray() {
    auto desc = reinterpret_cast<Desc *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_ARRAY_CAP));
    auto grow = reinterpret_cast<DescGrow_t>(Offsets::FUN_ADDON_ARRAY_GROW);
    desc->count = 0;
    ForEachEntry([&](uintptr_t entry) {
        if (*reinterpret_cast<const uint8_t *>(
                entry + Offsets::OFF_ADDON_ENTRY_FILTER_OUT) != 0)
            return;
        DescAppend(desc,
                   *reinterpret_cast<const uint32_t *>(
                       entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR),
                   grow);
    });
    auto qsort = reinterpret_cast<Qsort_t>(Offsets::FUN_CRT_QSORT);
    qsort(desc->data, desc->count, 4,
          reinterpret_cast<void *>(Offsets::FUN_ADDON_NAME_COMPARE));
}

void Run() {
    // Registry populated (login scan ran) and loose index built — both
    // always true by the first in-world LoadScriptFunctions, but these
    // are the states the steps below mutate, so gate explicitly.
    if (*reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_ADDON_INITIALIZED)) == 0 ||
        *reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_VFS_INDEX_READY)) == 0)
        return;

    RefreshLooseFileIndex();

    std::vector<uintptr_t> before;
    ForEachEntry([&before](uintptr_t entry) { before.push_back(entry); });

    // Replay login scan walk #2 verbatim: engine walker + engine
    // callback + engine parser. The parser's dedup guard makes this a
    // hash lookup per already-registered addon; new folders register
    // as complete, normal entries (their TOC is readable now — and it
    // goes through the FlavorToc/TocRewrite read hooks like any other).
    auto scan =
        reinterpret_cast<ScanDiskDirs_t>(Offsets::FUN_ADDON_SCAN_DISK_DIRS);
    scan(reinterpret_cast<const char *>(Offsets::VAR_ADDON_PATH_PREFIX),
         reinterpret_cast<const char *>(Offsets::VAR_ADDON_SCAN_PATTERN),
         reinterpret_cast<void *>(Offsets::FUN_ADDON_DISK_DIR_CB),
         /*userParam=*/nullptr, /*includeHidden=*/0);

    std::vector<uintptr_t> added;
    ForEachEntry([&](uintptr_t entry) {
        if (std::find(before.begin(), before.end(), entry) == before.end())
            added.push_back(entry);
    });
    if (added.empty())
        return;

    FixupReverseLoadWith(added);
    RebuildDisplayArray();
}

const Game::ModuleAutoRegister _autoreg{&Run};

} // namespace

} // namespace Addons::Rescan
