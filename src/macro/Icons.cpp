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

// `GetMacroIcons(list)` / `GetMacroItemIcons(list)` /
// `GetLooseMacroIcons(list)` / `GetLooseMacroItemIcons(list)` —
// the modern 4-function append-to-table icon enumeration surface.
// Each takes a Lua table as its only argument and appends icon
// basenames; returns nothing (mutation is the contract).
//
// Modern WoW's `IconDataProviderMixin` (see
// `AddOns/!!!ClassicAPI/Util/IconDataProvider.lua`) calls all four
// in this order to assemble its `BaseIconFilenames` cache:
//
//     GetLooseMacroIcons(spellList)
//     GetLooseMacroItemIcons(itemList)
//     GetMacroIcons(spellList)
//     GetMacroItemIcons(itemList)
//
// The split is `loose` (icons dropped onto disk under
// `Interface\Icons\` by the user) vs MPQ (icons baked into the game's
// MPQ archives), crossed with `Spell` (basenames starting with
// `Ability_` / `Spell_`) vs `Item` (basenames starting with `INV_`).
// Modern engines maintain four parallel arrays in their loader; we
// replicate the same scheme by hooking vanilla's three scan
// callbacks and tagging each captured filename by its callback
// source and basename prefix.
//
// **Data source (vanilla 1.12 / Octo).** `FUN_LOAD_MACRO_ICONS` runs
// three enumeration passes; which WALKER the loader hands a callback
// to is what makes it a loose or an MPQ source (see the block at
// `VAR_MACRO_ICON_COUNT` in Offsets.h for the full trail):
//
// - `FUN_MACRO_ICON_CB_MPQ` (`0x004F0220`) — pass 1, handed to
//   `FUN_MPQ_ENUM_FILES`, so it walks every mounted ARCHIVE's
//   `(listfile)`. Arg is the full `"Interface\Icons\<name>.blp"`
//   archive path. Tagged `mpq`.
// - `FUN_MACRO_ICON_CB_DISK_PREFIXED` (`0x004F0350`) — pass 2, handed
//   to the DISK walker on `<basePath>Interface\Icons\`. Record arg.
//   Tagged `loose`.
// - `FUN_MACRO_ICON_CB_DISK_ANY` (`0x004F04F0`) — pass 3, the DISK
//   walker again on the relative `Interface\Icons\`. Record arg.
//   Tagged `loose`.
//
// Passes 2 and 3 address the same folder (absolute vs relative), so a
// loose file reaches this module twice; the per-bucket dedup collapses
// it. They differ only in the ENGINE's filter — pass 2 keeps just
// `Ability_*`/`Spell_*`, pass 3 keeps any `.blp`/`.tga` — which is why
// a loose `INV_*` icon reaches the engine's own list but an archive one
// never does.
//
// We hook all three at ENTRY, before those filters, so every filename
// the engine walks is seen — including the ~5,226 archive `INV_*` names
// pass 1 rejects, which is what makes `GetMacroItemIcons` possible at
// all. Each hook classifies by prefix (`INV_` → item,
// `Ability_`/`Spell_` → spell, else ignore) and appends to the matching
// side array; a per-array `unordered_set` dedups on insert. The arrays
// are sorted lazily on first Lua read so output order matches modern
// engines.
//
// The source tags were previously inverted — the archive pass was tagged
// `loose` and both disk passes `mpq` — which swapped what
// `GetMacroIcons` and `GetLooseMacroIcons` returned. It went unnoticed
// because `IconDataProviderMixin` calls all four and concatenates, so
// only the ordering within each list changed, not the content.
//
// Entry shape (all four functions): uppercase basename stripped of
// the `Interface\Icons\` prefix and any `.blp`/`.tga` extension —
// e.g. `"INV_SWORD_25"`, `"ABILITY_KICK"`. Callers concat the
// prefix themselves before `texture:SetTexture(...)`.

#include "Game.h"
#include "Offsets.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace Macro::Icons {

namespace {

using LoadIcons_t = void(__cdecl *)();

void EnsureLoaded() {
    if (*reinterpret_cast<const uint32_t *>(
            static_cast<uintptr_t>(Offsets::VAR_MACRO_ICON_COUNT)) != 0)
        return;
    reinterpret_cast<LoadIcons_t>(Offsets::FUN_LOAD_MACRO_ICONS)();
}

constexpr int ICON_BUF_LEN = 128;

// Last path-component of `s` (after the final `\` or `/`), or `s`
// itself if no separator. NULL-safe.
const char *Basename(const char *s) {
    if (s == nullptr)
        return nullptr;
    const char *bn = s;
    for (const char *p = s; *p; ++p) {
        if (*p == '\\' || *p == '/')
            bn = p + 1;
    }
    return bn;
}

// Case-insensitive prefix match. `prefix` must be uppercase ASCII.
bool StartsWithCI(const char *s, const char *prefix) {
    for (int i = 0; prefix[i] != '\0'; ++i) {
        char c = s[i];
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - ('a' - 'A'));
        if (c != prefix[i])
            return false;
    }
    return true;
}

// Normalize an icon path for dedup: strip `Interface\Icons\` (or any
// directory) prefix, strip extension at first `.`, uppercase ASCII.
// `dst` must be `ICON_BUF_LEN` bytes. Result is null-terminated.
void NormalizeIconKey(char *dst, const char *src) {
    const char *bn = Basename(src);
    if (bn == nullptr) {
        dst[0] = '\0';
        return;
    }
    int i = 0;
    while (i + 1 < ICON_BUF_LEN && bn[i] != '\0' && bn[i] != '.') {
        char c = bn[i];
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - ('a' - 'A'));
        dst[i] = c;
        ++i;
    }
    dst[i] = '\0';
}

// =============================================================
// Four side arrays — (loose|mpq) × (spell|item). Each is a
// dedup-on-insert set plus a parallel vector preserving insertion
// order. We sort each vector once before the first Lua read so
// `GetMacroIcons` etc. return alphabetical results matching modern
// engines.
// =============================================================

enum Source { SRC_LOOSE = 0, SRC_MPQ = 1 };
enum Category { CAT_SPELL = 0, CAT_ITEM = 1 };

struct Bucket {
    std::vector<std::string> vec;
    std::unordered_set<std::string> set;
};

Bucket &BucketFor(Source src, Category cat) {
    static Bucket buckets[2][2];
    return buckets[src][cat];
}

void Capture(const char *raw, Source src) {
    const char *bn = Basename(raw);
    if (bn == nullptr || *bn == '\0')
        return;
    Category cat;
    if (StartsWithCI(bn, "INV_"))
        cat = CAT_ITEM;
    else if (StartsWithCI(bn, "ABILITY_") || StartsWithCI(bn, "SPELL_"))
        cat = CAT_SPELL;
    else
        return;
    char key[ICON_BUF_LEN];
    NormalizeIconKey(key, bn);
    if (key[0] == '\0')
        return;
    Bucket &b = BucketFor(src, cat);
    auto [it, inserted] = b.set.emplace(key);
    if (inserted)
        b.vec.emplace_back(*it);
}

// Sort all four buckets alphabetically. Called once before the first
// Lua read so output is stable and matches modern engines' sorted
// output. Idempotent — re-running re-sorts (cheap for already-sorted
// vectors) but `EnsureSorted` short-circuits via the flag.
bool g_sorted = false;
void EnsureSorted() {
    if (g_sorted)
        return;
    for (int s = 0; s < 2; ++s) {
        for (int c = 0; c < 2; ++c) {
            auto &v = BucketFor(static_cast<Source>(s),
                                 static_cast<Category>(c)).vec;
            std::sort(v.begin(), v.end());
        }
    }
    g_sorted = true;
}

// =============================================================
// Engine scan-callback hooks.
// =============================================================

// Archive-scan callback (`__fastcall(const char *archivePath)`). Path
// is `"Interface\Icons\<NAME>.blp"`-shaped — Basename() strips the
// prefix before classification.
using ArchiveCb_t = int (__fastcall *)(const char *path);
ArchiveCb_t IconCbMpq_o = nullptr;
int __fastcall IconCbMpq_h(const char *path) {
    Capture(path, SRC_MPQ);
    return IconCbMpq_o(path);
}

// Disk-scan callbacks (`__fastcall(FindRecord *)`). Record layout:
// `+0x04` = file flags (bit `0x10` = directory), `+0x08` = inline
// null-terminated filename (no prefix, no extension stripped).
using DiskCb_t = int (__fastcall *)(uint8_t *record);
DiskCb_t IconCbDiskPrefixed_o = nullptr;
int __fastcall IconCbDiskPrefixed_h(uint8_t *record) {
    if (record != nullptr && (record[4] & 0x10) == 0)
        Capture(reinterpret_cast<const char *>(record + 8), SRC_LOOSE);
    return IconCbDiskPrefixed_o(record);
}
DiskCb_t IconCbDiskAny_o = nullptr;
int __fastcall IconCbDiskAny_h(uint8_t *record) {
    if (record != nullptr && (record[4] & 0x10) == 0)
        Capture(reinterpret_cast<const char *>(record + 8), SRC_LOOSE);
    return IconCbDiskAny_o(record);
}

const Game::HookAutoRegister _hookCbMpq{
    Offsets::FUN_MACRO_ICON_CB_MPQ,
    reinterpret_cast<void *>(&IconCbMpq_h),
    reinterpret_cast<void **>(&IconCbMpq_o)};
const Game::HookAutoRegister _hookCbDiskPrefixed{
    Offsets::FUN_MACRO_ICON_CB_DISK_PREFIXED,
    reinterpret_cast<void *>(&IconCbDiskPrefixed_h),
    reinterpret_cast<void **>(&IconCbDiskPrefixed_o)};
const Game::HookAutoRegister _hookCbDiskAny{
    Offsets::FUN_MACRO_ICON_CB_DISK_ANY,
    reinterpret_cast<void *>(&IconCbDiskAny_h),
    reinterpret_cast<void **>(&IconCbDiskAny_o)};

// =============================================================
// Lua surface — four global mutators.
// =============================================================

// Walks `list[1..]` to find the first nil slot — that's where we
// start appending. Leaves the stack unchanged on return.
int FindAppendIndex(void *L) {
    int idx = 1;
    while (true) {
        Game::Lua::PushNumber(L, static_cast<double>(idx));
        Game::Lua::RawGet(L, 1);
        const int t = Game::Lua::Type(L, -1);
        Game::Lua::SetTop(L, -2);
        if (t == Game::Lua::TYPE_NIL)
            return idx;
        ++idx;
    }
}

void AppendBucket(void *L, Source src, Category cat, const char *usage) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, usage);
        return;
    }
    EnsureLoaded();
    EnsureSorted();
    const auto &v = BucketFor(src, cat).vec;
    if (v.empty())
        return;
    int idx = FindAppendIndex(L);
    for (const auto &name : v) {
        Game::Lua::PushNumber(L, static_cast<double>(idx++));
        Game::Lua::PushString(L, name.c_str());
        Game::Lua::RawSet(L, 1);
    }
}

int __fastcall Script_GetMacroIcons(void *L) {
    AppendBucket(L, SRC_MPQ, CAT_SPELL, "Usage: GetMacroIcons(table)");
    return 0;
}

int __fastcall Script_GetMacroItemIcons(void *L) {
    AppendBucket(L, SRC_MPQ, CAT_ITEM, "Usage: GetMacroItemIcons(table)");
    return 0;
}

int __fastcall Script_GetLooseMacroIcons(void *L) {
    AppendBucket(L, SRC_LOOSE, CAT_SPELL, "Usage: GetLooseMacroIcons(table)");
    return 0;
}

int __fastcall Script_GetLooseMacroItemIcons(void *L) {
    AppendBucket(L, SRC_LOOSE, CAT_ITEM,
                 "Usage: GetLooseMacroItemIcons(table)");
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMacroIcons", &Script_GetMacroIcons);
    Game::Lua::RegisterGlobalFunction("GetMacroItemIcons",
                                       &Script_GetMacroItemIcons);
    Game::Lua::RegisterGlobalFunction("GetLooseMacroIcons",
                                       &Script_GetLooseMacroIcons);
    Game::Lua::RegisterGlobalFunction("GetLooseMacroItemIcons",
                                       &Script_GetLooseMacroItemIcons);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Icons
