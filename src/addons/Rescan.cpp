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
//      loose-file hash index (see Offsets.h, `VAR_VFS_INDEX_BUILT`); a
//      file created after the index was built — a new addon's TOC, a new
//      .lua in an existing addon — is invisible until it is in the index.
//      3.80.2 has the same kind of table (the CAS loose map, CAS +0x108)
//      and fills it the same way at every registry rebuild: before it
//      scans the addons, `0x141AF9AE0` walks `Interface/AddOns/` (each
//      folder below it, 17 deep at most, through `0x141AF9560`) and adds
//      every file with `0x142A2A2D0`, which keeps an entry already there.
//      We do the same with 1.12's own walker: `FUN_VFS_INDEX_ADD_FOLDER`
//      on `Interface\AddOns`, from the root the build used, so new files
//      get the keys a full build gives them. The rest of the index (the
//      install folder and `Data\`, ~20k files) is not walked again.
//      New files in EXISTING addons need nothing more — the per-addon
//      loader `FUN_0051F240` re-reads the TOC from disk on every load
//      pass, so once the files are visible they load.
//      3.80.2's callback also decides which folders it enters: none whose
//      name starts with a dot (`.git`), and none 17 or more levels below
//      `Interface/AddOns/` (the addon folders are depth 1, and it recurses
//      while depth < 17). 1.12's walker enters every folder but `Data\`
//      through one call to itself (`SITE_VFS_INDEX_SUBFOLDER`), so that
//      call is redirected to the same rules for folders below
//      `Interface\AddOns\`. Every walk of that tree takes them, the
//      engine's own rebuilds after its clears included.
//      One difference remains, 1.12's own index rule: a deleted file keeps
//      its entry, and its open fails (1.12 does not fall back to the
//      archives after an index hit, where 3.80.2 checks the disk and
//      does).
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
//   3. METADATA IMMUTABILITY. The parser's dedup guard makes a
//      registered entry's `##` metadata read-once: an edited
//      `## SavedVariables:` / `## Dependencies:` / any `##` line never
//      re-parses. The engine's own complete per-entry destructor
//      (`FUN_ADDON_ENTRY_DESTROY` — frees every owned allocation and
//      self-unlinks from both the list and the hash; see Offsets.h)
//      makes evict + re-register safe: destroy the entry so the dedup
//      guard MISSES, re-feed the name to `FUN_TOC_PARSER`, and the
//      entry rebuilds from the edited TOC exactly like a fresh
//      registration. Gated on an actual `##` change (hash of the TOC's
//      `##` lines only — file-reference edits load natively and never
//      trigger the evict path). A TOC that no longer reads at all
//      (folder deleted) evicts without re-registering — the deletion
//      analog; a folder the scan still finds simply re-registers.
//
// Runs from the `ModuleAutoRegister` callback — LoadScriptFunctions
// post-hook, which fires inside FrameXML init (`FUN_0048FBF0`) BEFORE
// the AddOns.txt enable-state re-read, the load-progress count, and the
// addon load pass, on both login and every /reload. The engine then
// loads new entries natively: dep ordering, Bindings.xml, flavor TOCs,
// SavedVariables, ADDON_LOADED, the load-screen "Loading add-on %s".
// The window is also what makes eviction safe: the unload pass already
// ran (SavedVariables written from the OLD entry's SV lists, loaded
// bytes cleared), and the load pass hasn't — nothing holds entry state.
//
// It also runs from the glue registration pass (`GlueModuleAutoRegister`,
// every glue boot), so a return to character select shows the addons on
// disk, as 3.80.2 rebuilds its registry at every glue Resume
// (FUN_141390DB0 → FUN_141AF9AE0) as well as at every world UI init.
// 1.12 builds its registry only at account login (DefaultServerLogin →
// 0x0051C740), so the first glue boot, before any login, finds no
// registry and the gate below skips it. No addon is loaded on the glue
// screen (CGGameUI::Shutdown ran the unload pass), so the eviction window
// holds there too.
//
// Excluded from the evict path (everything else is fair game):
//   - the embedded addons — `Addons::Embedded` owns their entries
//     (registered first + filter byte);
//   - `## Secure:` / SMSG-managed entries — the parser cannot restore
//     packet-delivered state (see OFF_ADDON_ENTRY_SECURE).
//
// NOTE the surgical discipline. A previous attempt re-invoked the
// login-only teardown+rescan (`LoadAddOnsForAccount`, `0x0051C740`)
// mid-/reload and corrupted the registry (duplicate loads + Lua memory explosion on the
// NEXT login). This module never runs bulk teardown; the only entry
// mutation beyond what a login scan produces is the per-entry evict
// above, which is (a) the engine's own complete destructor, (b) always
// preceded by the reverse-LoadWith scrub (the one reference the dtor
// can't clean), and (c) always followed by the same fix-ups a new
// registration gets. New entries come from the same parser call
// `Addons::Embedded` has exercised every login.
//
// Thread note: the loose-file index has no lock in 1.12. We add to it on
// the main thread during the reload loading screen — the same context
// in which the engine's own unload pass clears it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "addons/LoadGate.h"
#include "addons/Embedded.h"
#include "storm/File.h"
#include "addons/Registry.h"
#include "storm/Memory.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Addons::Rescan {

namespace {

// ── Engine entry points (all verified by disassembly; see Offsets.h) ──

using IndexAddFolder_t = void(__fastcall *)(const char *root, const char *prefix, void *listing);
using SDirOpen_t = void *(__stdcall *)(const char *path);
using SDirClose_t = void(__stdcall *)(void *listing);
using ScanDiskDirs_t = int(__fastcall *)(const char *basePath,
                                         const char *pattern, void *callback,
                                         void *userParam, int includeHidden);
using ResolveByName_t = void *(__fastcall *)(const char *name);
using DescGrow_t = void(__thiscall *)(void *desc, uint32_t newCap);
using QuantumCalc_t = uint32_t(__thiscall *)(void *desc, uint32_t needed);
using Qsort_t = void(__cdecl *)(void *base, uint32_t num, uint32_t width,
                                void *compare);
using TocParser_t = void(__fastcall *)(const char *name);
using EntryDestroy_t = void(__stdcall *)(void *entry);

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

// The registry linked-list walk lives in `addons/Registry.h`
// (`Addons::ForEachEntry`), shared with `Addons::Embedded`.

// By-name registry lookup via the engine's own hash resolver (returns
// the entry's RequiredDeps desc, so subtract its offset — same recovery
// `Addons::Info` uses).
uintptr_t ResolveEntryByName(const char *name) {
    auto resolve =
        reinterpret_cast<ResolveByName_t>(Offsets::FUN_ADDON_RESOLVE_REQ_DEPS);
    const uintptr_t desc = reinterpret_cast<uintptr_t>(resolve(name));
    return desc != 0 ? desc - Offsets::OFF_ADDON_REQDEPS_DESC : 0;
}

// ── Step 1: add the AddOns folder to the loose-file index ─────────────

constexpr const char *kAddOnsFolder = "Interface\\AddOns";
constexpr const char kAddOnsPrefix[] = "Interface\\AddOns\\";
constexpr int kMaxAddOnsDepth = 17; // 0x141AF9560: recurses while depth < 0x11

// 0x141AF9560's folder rules, for a folder `rel` below the index root.
bool IsSkippedAddOnsFolder(const char *rel) {
    constexpr size_t prefixLength = sizeof(kAddOnsPrefix) - 1;
    if (_strnicmp(rel, kAddOnsPrefix, prefixLength) != 0)
        return false;
    const char *name = rel + prefixLength;
    int depth = 1;
    for (const char *c = name; *c != '\0'; ++c) {
        if (*c == '\\') {
            ++depth;
            name = c + 1;
        }
    }
    return name[0] == '.' || depth >= kMaxAddOnsDepth;
}

// SITE_VFS_INDEX_SUBFOLDER: the walker's call into a subfolder. Same ABI as
// the walker; a skipped folder returns, and the caller closes its listing.
void __fastcall IndexSubfolder(const char *root, const char *rel, void *listing) {
    if (IsSkippedAddOnsFolder(rel))
        return;
    reinterpret_cast<IndexAddFolder_t>(Offsets::FUN_VFS_INDEX_ADD_FOLDER)(root, rel, listing);
}

// Walks `Interface\AddOns` into the index as the build would have. Before
// the build there is nothing to add to: the first lookup builds the whole
// index, this folder included. The root is the build's: the base path, or
// "." when that cannot be listed (0x00646EA0).
void IndexAddOnsFolder() {
    Common::RedirectCall(Offsets::SITE_VFS_INDEX_SUBFOLDER, Offsets::FUN_VFS_INDEX_ADD_FOLDER,
                         reinterpret_cast<const void *>(&IndexSubfolder));
    if (Game::Read<uint8_t>(Offsets::VAR_VFS_INDEX_BUILT) == 0)
        return;
    const auto open = reinterpret_cast<SDirOpen_t>(Offsets::FUN_SDIR_OPEN);
    const auto close = reinterpret_cast<SDirClose_t>(Offsets::FUN_SDIR_CLOSE);

    const char *root = reinterpret_cast<const char *>(Offsets::VAR_VFS_BASE_PATH);
    if (void *listing = open(root))
        close(listing);
    else
        root = reinterpret_cast<const char *>(Offsets::VAR_VFS_BASE_FALLBACK);

    char folder[260];
    const size_t length = std::strlen(root);
    const bool slash = length != 0 && root[length - 1] == '\\';
    std::snprintf(folder, sizeof(folder), "%s%s%s", root, slash ? "" : "\\", kAddOnsFolder);
    void *listing = open(folder);
    if (listing == nullptr)
        return;
    reinterpret_cast<IndexAddFolder_t>(Offsets::FUN_VFS_INDEX_ADD_FOLDER)(root, kAddOnsFolder,
                                                                         listing);
    close(listing);
}

// ── Step 1.5: `##` metadata refresh (evict + re-register on change) ──

// name → FNV-1a hash of the addon's `##` lines, as last parsed.
// Per-process on purpose: the registry persists across /reload, and a
// stale hash after a full re-login scan only causes one harmless
// re-parse of already-current metadata.
std::unordered_map<std::string, uint32_t> g_metaHash;

// Read `<name>`'s base TOC exactly as the parser does — through the
// (hooked) `FUN_FILE_READ`, so flavor selection and TOC rewriting are
// reflected in what gets hashed. Caller frees `*buf` via Storm.
bool ReadToc(const char *name, void **buf, size_t *size) {
    char path[260];
    std::snprintf(path, sizeof(path), "Interface\\AddOns\\%s\\%s.toc", name,
                  name);
    *size = 0;
    return Storm::File::Read(path, buf, size, 1);
}

// FNV-1a over the TOC's `##` lines only. File-reference lines are
// excluded on purpose — the load pass re-reads those from disk every
// /reload anyway, so an edit there must not trigger the evict path.
// Line terminators are excluded (a CRLF↔LF conversion is not a
// metadata change); a per-line separator keeps adjacent lines from
// concatenating into the same hash. The BOM skip mirrors the parser.
uint32_t HashTocMetadata(const char *buf, size_t size) {
    uint32_t h = 2166136261u;
    size_t i = 0;
    if (size >= 3 && static_cast<uint8_t>(buf[0]) == 0xEF &&
        static_cast<uint8_t>(buf[1]) == 0xBB &&
        static_cast<uint8_t>(buf[2]) == 0xBF)
        i = 3;
    while (i < size) {
        size_t end = i;
        while (end < size && buf[end] != '\n')
            ++end;
        size_t stop = end;
        while (stop > i && buf[stop - 1] == '\r')
            --stop;
        if (stop - i >= 2 && buf[i] == '#' && buf[i + 1] == '#') {
            for (size_t k = i; k < stop; ++k) {
                h ^= static_cast<uint8_t>(buf[k]);
                h *= 16777619u;
            }
            h ^= '\n';
            h *= 16777619u;
        }
        i = end + 1;
    }
    return h;
}

// True when the entry never takes the evict path: the embedded addons
// (owned by `Addons::Embedded` — registered first + filter byte) and
// SMSG-managed secure entries (the parser cannot restore packet-delivered
// state).
bool IsRefreshExempt(uintptr_t entry, const char *name) {
    return name == nullptr || Addons::Embedded::IsEmbedded(name) ||
           *reinterpret_cast<const uint8_t *>(
               entry + Offsets::OFF_ADDON_ENTRY_SECURE) != 0;
}

// Read + hash `<name>`'s `##` lines. False when the TOC doesn't read.
bool TryHashToc(const char *name, uint32_t *outHash) {
    void *buf = nullptr;
    size_t size = 0;
    if (!ReadToc(name, &buf, &size))
        return false;
    *outHash = HashTocMetadata(static_cast<const char *>(buf), size);
    Storm::Free(buf, __FILE__, __LINE__);
    return true;
}

// Remove every pointer to `victim` from the OTHER entries'
// reverse-LoadWith lists — the one reference `FUN_ADDON_ENTRY_DESTROY`
// cannot clean (it frees the victim's own list, not the victim's
// presence in others'). Without this, the loader's post-ADDON_LOADED
// loop dereferences freed memory.
void ScrubReverseLoadWith(uintptr_t victim) {
    ForEachEntry([victim](uintptr_t entry) {
        if (entry == victim)
            return;
        auto *desc = reinterpret_cast<Desc *>(
            entry + Offsets::OFF_ADDON_REVLOADWITH_DESC);
        uint32_t w = 0;
        for (uint32_t r = 0; r < desc->count; ++r)
            if (desc->data[r] != static_cast<uint32_t>(victim))
                desc->data[w++] = desc->data[r];
        desc->count = w;
    });
}

// The refresh pass. Walks the registry comparing each entry's current
// `##` hash against the last-parsed one; on change (or an unreadable
// TOC — deleted folder), evicts via the engine's own destructor and,
// for changes, re-feeds the name to the parser. Re-registered entries
// are appended to `refreshed` so they join the same reverse-LoadWith /
// display-array fix-ups a new folder gets. Returns true when anything
// was actually evicted (the display array holds a freed name pointer
// until it's rebuilt).
//
// Victims are collected during the walk and processed after it — the
// destructor unlinks the entry the iterator stands on (its next-pointer
// is zeroed), so evicting mid-walk would silently truncate the walk.
// Names are copied for the same reason: the dtor frees the entry's
// name string. `g_metaHash` is committed only once the re-registration
// actually happened — updating it during the walk would absorb the edit
// forever if any later step failed, with no retry on the next /reload.
bool RefreshChangedMetadata(std::vector<uintptr_t> &refreshed) {
    struct Victim {
        std::string name;
        uint32_t newHash;
        bool reRegister; // false: TOC unreadable — evict only
    };
    std::vector<Victim> victims;
    ForEachEntry([&victims](uintptr_t entry) {
        const char *name = *reinterpret_cast<const char *const *>(
            entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
        if (IsRefreshExempt(entry, name))
            return;
        uint32_t h = 0;
        if (!TryHashToc(name, &h)) {
            victims.push_back({name, 0, false});
            return;
        }
        auto it = g_metaHash.find(name);
        if (it == g_metaHash.end())
            g_metaHash.emplace(name, h); // first sighting — seed only
        else if (it->second != h)
            victims.push_back({name, h, true});
    });

    bool anyEvicted = false;
    auto destroy =
        reinterpret_cast<EntryDestroy_t>(Offsets::FUN_ADDON_ENTRY_DESTROY);
    auto parse = reinterpret_cast<TocParser_t>(Offsets::FUN_TOC_PARSER);
    for (const Victim &v : victims) {
        const uintptr_t entry = ResolveEntryByName(v.name.c_str());
        if (entry == 0)
            continue;
        ScrubReverseLoadWith(entry);
        destroy(reinterpret_cast<void *>(entry));
        anyEvicted = true;
        if (!v.reRegister) {
            g_metaHash.erase(v.name);
            continue;
        }
        // The parser appends the fresh entry at the list TAIL, so a
        // refreshed addon's position in the load pass changes for this
        // reload. Declared dependencies still force-load first; only
        // undeclared load-order assumptions between unrelated addons
        // could observe the difference.
        parse(v.name.c_str());
        const uintptr_t fresh = ResolveEntryByName(v.name.c_str());
        if (fresh != 0) {
            refreshed.push_back(fresh);
            g_metaHash[v.name] = v.newHash;
        } else {
            // Evicted but the parse didn't restore it (TOC became
            // unreadable between hash and parse) — treat as deleted so
            // a reappearing folder re-seeds instead of diffing against
            // a hash the registry never absorbed.
            g_metaHash.erase(v.name);
        }
    }
    return anyEvicted;
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
// list, skipping filtered entries (keeps the embedded addons hidden), then
// sort with the engine's own comparator to restore alphabetical order.
void RebuildDisplayArray() {
    AddOns::LoadGate::ApplyListFilter(); // hides wrong-game-type addons first
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
    // Registry populated (login scan ran) — always true by the first
    // in-world LoadScriptFunctions, false at the first glue boot before
    // any account login, and it is the state the steps below mutate.
    if (*reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_ADDON_INITIALIZED)) == 0)
        return;

    IndexAddOnsFolder();

    // Metadata refresh runs BEFORE the snapshot: a re-registered entry
    // is alive by snapshot time, so it lands in `before` and can never
    // double-count in `added` — even if the allocator reuses the old
    // entry's address, `refreshed` tracks it independently of the diff.
    std::vector<uintptr_t> refreshed;
    const bool evicted = RefreshChangedMetadata(refreshed);

    std::vector<uintptr_t> before;
    ForEachEntry([&before](uintptr_t entry) { before.push_back(entry); });

    // Replay login scan walk #2 verbatim: engine walker + engine
    // callback + engine parser. The parser's dedup guard makes this a
    // hash lookup per already-registered addon; new folders register
    // as complete, normal entries (their TOC is readable now — and it
    // goes through the FlavorToc/TocRewrite read hooks like any other).
    //
    // The dedup guard is why this replay can NEVER refresh or corrupt
    // an existing entry's `##` metadata (verified in the parser's
    // decompile): the guard is the function's FIRST block — hash
    // lookup + name compare, early `return` on match BEFORE the TOC
    // path is even built, so no file read and no entry write happen
    // for a registered name. Both the Storm hash (`FUN_0064B3F0`,
    // uppercases a–z and folds '/'→'\\' before hashing) and the
    // compare (`SStrCmpI`) are case-insensitive, so a case-renamed
    // folder still matches its existing entry rather than registering
    // a duplicate.
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

    // Seed the metadata map for entries this reload registered, so the
    // next reload diffs a `##` edit against what was actually parsed
    // (waiting for the next pass to seed would silently absorb an edit
    // made between now and then).
    for (uintptr_t entry : added) {
        const char *name = *reinterpret_cast<const char *const *>(
            entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
        uint32_t h = 0;
        if (!IsRefreshExempt(entry, name) && TryHashToc(name, &h))
            g_metaHash[name] = h;
    }

    if (added.empty() && refreshed.empty() && !evicted)
        return;

    // Refreshed entries need the same link fix-ups a new folder gets:
    // their old reverse-LoadWith pairs died with the old entry (scrub +
    // dtor), and the parser doesn't build reverse links.
    std::vector<uintptr_t> linked = added;
    linked.insert(linked.end(), refreshed.begin(), refreshed.end());
    if (!linked.empty())
        FixupReverseLoadWith(linked);

    // Rebuild whenever membership OR an entry identity changed — after
    // any eviction (including a pure deletion) the display array still
    // holds the dead entry's freed name pointer.
    RebuildDisplayArray();
}

const Game::ModuleAutoRegister _autoreg{&Run};
const Game::GlueModuleAutoRegister _glueAutoreg{&Run};

} // namespace

} // namespace Addons::Rescan
