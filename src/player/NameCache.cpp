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

// Persistent name cache — see NameCache.h for the motivating use case.
//
// Two persistence files:
//
//   `WTF\Account\<acct>\ClassicAPI.txt`
//       Account-level setting — `PersistentNameCacheEnabled`.
//       Shared with other modules via the
//       [`Settings::Account`](../settings/Account.h) registry; we
//       register a section at static-init time with reset / serialize /
//       parse callbacks, and the registry handles load/save so neither
//       this module nor `Charlist::Order` clobbers the other's lines.
//
//   `WTF\ClassicAPI\NameCache-<realm>@<realmList>.txt`
//       Per-realm cache contents, SHARED across every account on the
//       same server. Named by the realm plus the realmlist address
//       (unambiguous server identity — can't collide across unrelated
//       private servers), rather than by account, so alts on different
//       accounts pool into one file. WoW doesn't make `WTF\ClassicAPI\`,
//       so we create it. v5 format. One entry per line, fields
//       tab-separated (a realm can hold spaces, and is empty for a player
//       on this realm):
//           `guid  name  realm  classID  raceID  sex`
//       Loaded once the realm name is available post-login (migrating
//       the v4 file in on first load, and deleting it once v5 is saved;
//       see `V4CachePath`). Saved when
//       (a) a recent write hasn't been flushed AND (b) `kFlushIntervalMs`
//       has elapsed since the prior save, OR (c) DLL teardown / `Flush()`
//       is called.
//
// Population sources:
//   1. The engine's NameCache writer (`FUN_PLAYER_INFO_CACHE_WRITE`)
//      — every SMSG_NAME_QUERY_RESPONSE the engine processes flows
//      through here, and it is the only writer that fills an engine
//      NameCache row. Covers chat, group/raid sync, guild roster
//      updates, and every visible player (`CGUnit_C::GetUnitName`
//      0x00609210 sends the query on a miss).
//   2. `C_PlayerCache.RememberPlayer(guid, name, classToken)` from
//      Lua — lets addons feed in sources the engine cache doesn't
//      touch directly (e.g. /who results parsed via GetWhoInfo,
//      mouseover, target).
//   3. A one-time import of the engine NameCache when the cache is
//      turned on. The hook only sees responses that arrive while the
//      cache is enabled, so rows the engine resolved earlier in the
//      session would otherwise stay missing until that player is
//      queried again. The engine keeps names in memory only, so a
//      fresh session starts empty and needs no import.
//
// Entries are GUID-keyed, like the engine's own cache. Full names are the
// dedup signal though: names are unique per realm, so a fresh GUID claiming a
// full name we've already seen means the prior character was deleted and the
// name recycled. The old GUID is dead (server won't honor it again); keeping
// it just bloats the file. `Upsert` drops the old entry on each write so the
// cache holds one entry per full name.

#include "NameCache.h"

#include "FullName.h"
#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"
#include "guid/Guid.h"
#include "settings/Account.h"
#include "settings/Paths.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include <windows.h>

namespace Player::NameCache {

namespace {

// Backstop flush interval — explicit `Flush()` calls from DllMain's
// `FrameScript_Initialize` hook (covers /reload and /logout) and
// `DLL_PROCESS_DETACH` (clean /quit) are the primary save path. The
// in-`Remember` `MaybeFlush` runs only as a defensive measure for
// long sessions that never reload AND end via hard process kill
// (task manager, OS crash) — in that case we bound data loss to
// roughly this interval. 5 minutes was chosen as a compromise:
// long enough that routine play has zero in-flight I/O, short
// enough that a rare bad-shutdown loses at most ~5 min of new
// entries.
constexpr DWORD kFlushIntervalMs = 5 * 60 * 1000;

// ChrClasses.dbc max ID. Anything above this is a corrupt input.
constexpr uint32_t kMaxClassID = 16;
// ChrRaces.dbc max ID. Vanilla has 8 (Human..Troll); allow some
// headroom for private-server custom races.
constexpr uint32_t kMaxRaceID = 16;
// Wire sex values are 0/1. Any higher value is corrupt input.
constexpr uint32_t kMaxSex = 1;

constexpr const char *kCacheDir = "WTF\\ClassicAPI";

// Full names compare ignoring ASCII case, the same rule as the engine's own
// name compares (SStrCmpI 0x0064A4C0 is a null-checked CRT _strnicmp), so
// `"gedwyr"` finds `"Gedwyr"`. A re-cased name for the same GUID takes the
// rename path in Upsert, so the newest spelling wins.
struct NameHash {
    size_t operator()(const std::string &s) const {
        size_t h = 2166136261u; // FNV-1a over the lowercased bytes
        for (unsigned char c : s)
            h = (h ^ static_cast<size_t>(c >= 'A' && c <= 'Z' ? c + 32 : c)) * 16777619u;
        return h;
    }
};
struct NameEqual {
    bool operator()(const std::string &a, const std::string &b) const {
        return a.size() == b.size() && _strnicmp(a.c_str(), b.c_str(), a.size()) == 0;
    }
};
std::unordered_map<uint64_t, Entry> g_entries;
// Full name → GUID, maintained in lockstep with `g_entries`. Invariant:
// `g_byName[FullName::Make(e.name, e.realm)] == guid` iff
// `g_entries[guid] == e`.
std::unordered_map<std::string, uint64_t, NameHash, NameEqual> g_byName;
bool g_enabled = false;
// The cache file the store holds, empty until the first load. Saves go here,
// never to the current realm's file: a session can log out and into another
// realm, and LoadCacheIfNeeded swaps the store when it does.
std::string g_loadedPath;
bool g_dirty = false;
DWORD g_lastSaveTick = 0;
// The v4 file this session migrated from, deleted once the v5 file is saved.
std::string g_migratedV4Path;

// ----- path resolution -----

// realmList CVar value (the realmlist address, e.g.
// "logon.turtle-wow.org") — the unambiguous server identity. Unlike the
// friendly realm name it can't collide across unrelated private servers,
// so it's part of the shared cache's file name. Mirrors
// `Security::CredStore`'s realm scoping.
const char *ReadRealmList() {
    using GetRealmList_t = const char *(__fastcall *)(int forceRefresh);
    return reinterpret_cast<GetRealmList_t>(
        static_cast<uintptr_t>(Offsets::FUN_CVAR_REALM_LIST))(0);
}

// Filesystem-safe file-name part. Any character Windows rejects in a path —
// or that could surprise, like the apostrophe in "N'Zoth" or a realmlist's
// ":port" — becomes '_'. Deterministic, so a realm always maps to the same
// file.
std::string SanitizeComponent(const char *s) {
    std::string out;
    for (const char *p = s; p != nullptr && *p; ++p) {
        const char c = *p;
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                          c == '_';
        out.push_back(safe ? c : '_');
    }
    return out;
}

// Shared, server-specific cache path:
//   WTF\ClassicAPI\NameCache-<realm>@<realmList>.txt
// Not under Account\, so alts on different accounts of the same server
// share one pool. SanitizeComponent never writes '@', so the two names
// can't run into each other. Empty (defer) until both realmlist + realm
// are known.
std::string CachePath() {
    const char *realmList = ReadRealmList();
    if (realmList == nullptr || realmList[0] == '\0')
        return {};
    const std::string realm = Settings::Paths::RealmName();
    if (realm.empty())
        return {};
    std::string out = kCacheDir;
    out += "\\NameCache-";
    out += SanitizeComponent(realm.c_str());
    out += '@';
    out += SanitizeComponent(realmList);
    out += ".txt";
    return out;
}

// The v4 file the v5 one replaced, for the one-time migration:
//   WTF\ClassicAPI\<realmList>\<realm>\ClassicAPI_NameCache.txt
// Built exactly as v4 wrote it, so it matches the file already on disk.
std::string V4CachePath() {
    const char *realmList = ReadRealmList();
    if (realmList == nullptr || realmList[0] == '\0')
        return {};
    const std::string realm = Settings::Paths::RealmName();
    if (realm.empty())
        return {};
    std::string out = kCacheDir;
    out += '\\';
    out += SanitizeComponent(realmList);
    out += '\\';
    out += SanitizeComponent(realm.c_str());
    out += "\\ClassicAPI_NameCache.txt";
    return out;
}

// ----- account settings (registered with Settings::Account) -----

void SettingsReset() {
    g_enabled = false;
}

void SettingsSerialize(std::ostream &out) {
    out << "PersistentNameCacheEnabled=" << (g_enabled ? 1 : 0) << "\n";
}

bool SettingsParse(const std::string &key, const std::string &value) {
    if (key == "PersistentNameCacheEnabled") {
        g_enabled = (value == "1" || value == "true");
        return true;
    }
    return false;
}

const Settings::Account::AutoRegister _accountSettings{
    &SettingsReset, &SettingsSerialize, &SettingsParse};

// ----- cache load/save -----

// A name or realm for a cache line: tabs and line breaks dropped (they would
// break the line), and at most `size - 1` bytes, the engine's own capacity for
// the field (`PLAYER_INFO_NAME_SIZE` / `PLAYER_INFO_REALM_SIZE`), so anything
// the engine stored fits. Names are UTF-8, and a cut never splits a
// character: a 16-byte cap here garbled the last of six Chinese characters
// (issue #119).
std::string SanitizeField(const char *s, size_t size) {
    std::string out;
    for (const char *p = s; p != nullptr && *p != '\0'; ++p) {
        const char c = *p;
        if (c == '\t' || c == '\r' || c == '\n')
            continue;
        out.push_back(c);
    }
    if (out.size() >= size) {
        // Back up over continuation bytes (10xxxxxx) to the lead byte of the
        // character the cut would split.
        size_t cut = size - 1;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut;
        out.resize(cut);
    }
    return out;
}

std::string SanitizeName(const char *name) {
    return SanitizeField(name, Offsets::PLAYER_INFO_NAME_SIZE);
}

std::string SanitizeRealm(const char *realm) {
    return SanitizeField(realm, Offsets::PLAYER_INFO_REALM_SIZE);
}

// Inserts or updates the entry for `guid`, maintaining the `g_byName`
// invariant. Handles three cases:
//
//   1. Another GUID holds this full name (name recycled — character
//      deleted and recreated). Drop that entry.
//   2. This GUID holds another full name (character renamed, or a
//      re-cased spelling). Move it to the new name.
//   3. Same full name + same GUID — update in place.
//
// `classID` / `raceID` / `sex` of 0 mean "caller doesn't know" and
// don't overwrite an existing non-zero field. Returns true if any
// change occurred (caller marks the cache dirty for flushing).
bool Upsert(uint64_t guid, const std::string &name, const std::string &realm,
            uint32_t classID, uint32_t raceID, uint32_t sex) {
    if (name.empty() || guid == 0)
        return false;
    const std::string fullName = FullName::Make(name.c_str(), realm.c_str());
    bool changed = false;

    // Case 1: the full name under a different GUID — recycled name.
    auto nameIt = g_byName.find(fullName);
    if (nameIt != g_byName.end() && nameIt->second != guid) {
        g_entries.erase(nameIt->second);
        g_byName.erase(nameIt);
        changed = true;
    }

    // Case 2: this GUID under a different full name — rename.
    auto it = g_entries.find(guid);
    if (it == g_entries.end()) {
        it = g_entries.emplace(guid, Entry{name, realm, 0, 0, 0}).first;
        changed = true;
    } else if (it->second.name != name || it->second.realm != realm) {
        g_byName.erase(FullName::Make(it->second.name.c_str(), it->second.realm.c_str()));
        it->second.name = name;
        it->second.realm = realm;
        changed = true;
    }
    g_byName[fullName] = guid;

    // Case 3: update the fields in place.
    Entry &e = it->second;
    if (classID != 0 && e.classID != classID) {
        e.classID = classID;
        changed = true;
    }
    if (raceID != 0 && e.raceID != raceID) {
        e.raceID = raceID;
        changed = true;
    }
    // sex == 0 is ambiguous (male AND "unknown") — only treat
    // non-zero as authoritative.
    if (sex != 0 && e.sex != sex) {
        e.sex = sex;
        changed = true;
    }
    return changed;
}

// Parses v5 cache lines (`guid\tname\trealm\tclass\trace\tsex`) from `file`
// into the store. Split on tabs, not whitespace: a realm can hold spaces and
// is empty for a player on this realm. Variable-width hex GUID (no leading
// zeros).
void LoadFromStream(std::istream &file) {
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream is(line);
        std::string guidStr, name, realm, classStr, raceStr, sexStr;
        if (!std::getline(is, guidStr, '\t') || !std::getline(is, name, '\t') ||
            !std::getline(is, realm, '\t') || !std::getline(is, classStr, '\t') ||
            !std::getline(is, raceStr, '\t') || !std::getline(is, sexStr))
            continue;

        const uint64_t guid = std::strtoull(guidStr.c_str(), nullptr, 16);
        const uint32_t classID = static_cast<uint32_t>(std::strtoul(classStr.c_str(), nullptr, 10));
        const uint32_t raceID = static_cast<uint32_t>(std::strtoul(raceStr.c_str(), nullptr, 10));
        const uint32_t sex = static_cast<uint32_t>(std::strtoul(sexStr.c_str(), nullptr, 10));
        if (guid == 0 || classID > kMaxClassID || raceID > kMaxRaceID || sex > kMaxSex)
            continue;
        // Upsert handles the rare same-name / same-guid dedup if the
        // file has duplicate rows (shouldn't, but defensive).
        Upsert(guid, SanitizeName(name.c_str()), SanitizeRealm(realm.c_str()), classID,
               raceID, sex);
    }
}

// Parses v4 cache lines (`name\tguid\tclass\trace\tsex`) for the one-time
// migration. v4 kept no realm, so every entry is a player on this realm. Names
// hold no whitespace, so the whitespace split v4 itself used is safe here.
void LoadV4FromStream(std::istream &file) {
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream is(line);
        std::string name, guidStr, classStr, raceStr, sexStr;
        if (!(is >> name >> guidStr >> classStr >> raceStr >> sexStr))
            continue;

        const uint64_t guid = std::strtoull(guidStr.c_str(), nullptr, 16);
        const uint32_t classID = static_cast<uint32_t>(std::strtoul(classStr.c_str(), nullptr, 10));
        const uint32_t raceID = static_cast<uint32_t>(std::strtoul(raceStr.c_str(), nullptr, 10));
        const uint32_t sex = static_cast<uint32_t>(std::strtoul(sexStr.c_str(), nullptr, 10));
        if (guid == 0 || classID > kMaxClassID || raceID > kMaxRaceID || sex > kMaxSex)
            continue;
        Upsert(guid, SanitizeName(name.c_str()), std::string(), classID, raceID, sex);
    }
}

// Deletes the migrated v4 file, then its `<realm>` and `<realmList>` folders.
// RemoveDirectory only removes an empty folder, so a server whose other
// realms have not migrated yet keeps its folder.
void DeleteMigratedV4() {
    if (g_migratedV4Path.empty())
        return;
    DeleteFileA(g_migratedV4Path.c_str());
    std::string dir = g_migratedV4Path;
    for (int level = 0; level < 2; ++level) {
        dir.erase(dir.find_last_of('\\'));
        if (!RemoveDirectoryA(dir.c_str()))
            break;
    }
    g_migratedV4Path.clear();
}

void SaveCache() {
    if (g_loadedPath.empty())
        return;
    const std::string &path = g_loadedPath;
    // WoW doesn't make `WTF\ClassicAPI\`, and the ofstream open below
    // won't either. On an existing directory this is a no-op.
    CreateDirectoryA(kCacheDir, nullptr);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return;
        // GUID first since it's the key. Variable-width hex — 1-8 digits
        // for player GUIDs (the high dword is always 0), up to 16 if a
        // non-player GUID ever lands here.
        out << "# ClassicAPI name cache v5 (per-realm, shared across accounts)\n";
        out << "# guid\tname\trealm\tclassID\traceID\tsex\n";
        for (const auto &kv : g_entries) {
            const Entry &e = kv.second;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%llX\t",
                          static_cast<unsigned long long>(kv.first));
            out << buf << e.name << '\t' << e.realm;
            std::snprintf(buf, sizeof(buf), "\t%u\t%u\t%u\n", e.classID, e.raceID, e.sex);
            out << buf;
        }
        if (!out)
            return;
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
        return; // stays dirty, so the next flush tries again
    g_dirty = false;
    g_lastSaveTick = GetTickCount();
    DeleteMigratedV4();
}

// Makes the store hold the current realm's cache: loads it on first use, and
// again whenever the session has moved to another realm. The old realm's
// unsaved entries are saved to its own file first, so one realm's players
// never land in another realm's file.
void LoadCacheIfNeeded() {
    const std::string path = CachePath();
    if (path.empty() || path == g_loadedPath)
        return; // pre-realm (retry on next call), or already loaded
    if (g_dirty)
        SaveCache();
    g_entries.clear();
    g_byName.clear();
    g_dirty = false;
    g_migratedV4Path.clear();
    g_loadedPath = path;

    std::ifstream file(path);
    if (file.is_open()) {
        LoadFromStream(file);
        return;
    }

    // No v5 file yet. One-time migration: seed from the v4 file if it
    // exists, then mark dirty so the next save writes v5. That save
    // deletes the v4 file.
    const std::string v4Path = V4CachePath();
    std::ifstream v4File(v4Path);
    if (v4File.is_open()) {
        LoadV4FromStream(v4File);
        g_dirty = true;
        g_migratedV4Path = v4Path;
    }
}

void MaybeFlush() {
    if (!g_dirty)
        return;
    const DWORD now = GetTickCount();
    if ((now - g_lastSaveTick) < kFlushIntervalMs)
        return;
    SaveCache();
}

// ----- engine NameCache mirror -----

// Mirrors one engine player-info record into our store. `record` is
// either the SMSG_NAME_QUERY_RESPONSE buffer the writer consumes or the
// cache row it produced — same layout (`OFF_PLAYER_INFO_*`).
void RememberRecord(uint64_t guid, const uint8_t *record) {
    if (guid == 0)
        return;
    const char *name = reinterpret_cast<const char *>(
        record + Offsets::OFF_PLAYER_INFO_NAME);
    const char *realm = reinterpret_cast<const char *>(
        record + Offsets::OFF_PLAYER_INFO_REALM);
    const uint32_t classID = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_PLAYER_INFO_CLASS);
    const uint32_t raceID = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_PLAYER_INFO_RACE);
    const uint32_t sex = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_PLAYER_INFO_SEX);
    if (name[0] == '\0')
        return;
    if (classID == 0 || classID > kMaxClassID)
        return;

    LoadCacheIfNeeded();
    // Out-of-range race/sex mean the record didn't carry them (some
    // entry types skip these fields); pass 0 so `Upsert` preserves
    // any prior real data.
    const uint32_t safeRace = (raceID != 0 && raceID <= kMaxRaceID) ? raceID : 0;
    const uint32_t safeSex = (sex <= kMaxSex) ? sex : 0;
    if (Upsert(guid, SanitizeName(name), SanitizeRealm(realm), classID, safeRace, safeSex)) {
        g_dirty = true;
        MaybeFlush();
    }
}

// Copies every row the engine already resolved this session. Run when
// the cache turns on, since the hook missed those responses.
void ImportEngineCache() {
    Unit::Identity::ForEachCachedPlayer(
        [](uint64_t guid, const uint8_t *record, void *) {
            RememberRecord(guid, record);
            return true;
        },
        nullptr);
}

// `__thiscall(this = cache, packet, guidLo, guidHi)`. We forward to
// the original first (so the engine settles its entry and dispatches
// callbacks), then mirror the packet's fields into our store.
using CacheWrite_t = void(__thiscall *)(void *self, const uint8_t *packet,
                                        uint32_t guidLo, uint32_t guidHi);
CacheWrite_t CacheWrite_o = nullptr;

void __fastcall CacheWrite_h(void *self, void *edx, const uint8_t *packet,
                             uint32_t guidLo, uint32_t guidHi) {
    // __fastcall stub forwards to __thiscall — MSVC's free-function
    // __fastcall lays out (ECX=self, EDX=unused, stack=packet, lo, hi),
    // matching the engine's __thiscall(self, packet, lo, hi).
    CacheWrite_o(self, packet, guidLo, guidHi);
    (void)edx;

    if (packet == nullptr || !IsEnabled())
        return;
    RememberRecord((static_cast<uint64_t>(guidHi) << 32) | guidLo, packet);
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_PLAYER_INFO_CACHE_WRITE,
    reinterpret_cast<void *>(&CacheWrite_h),
    reinterpret_cast<void **>(&CacheWrite_o)};

// ----- Lua surface -----

// Thin shim around the centralized `Guid::Parse` so this TU's Lua
// surface keeps a local symbol with its existing call shape.
bool ParseGUID(const char *str, uint64_t *outGUID) {
    return Guid::Parse(str, outGUID);
}

// `C_PlayerCache.RememberPlayer(guid, name, classToken [, raceToken
// [, sex]])` — stores the entry in the persistent cache. `name` is the bare
// name of a player on this realm, or "Name-Realm" for one from another, as
// chat events name them. Class and race tokens are uppercase engine tokens
// (`"WARRIOR"`, `"NIGHTELF"`, etc.); sex is `0` (male) or `1` (female),
// matching the wire-format convention the engine cache stores. Trailing args
// are optional. Returns `true` on success, `false` if the cache is disabled
// or the required args are malformed.
int __fastcall Script_C_PlayerCache_RememberPlayer(void *L) {
    if (!IsEnabled()) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)
            || !Game::Lua::IsString(L, 3)) {
        Game::Lua::Error(L,
            "Usage: C_PlayerCache.RememberPlayer(guid, name, classToken [, raceToken [, sex]])");
        return 0;
    }
    uint64_t guid;
    if (!ParseGUID(Game::Lua::ToString(L, 1), &guid) || guid == 0) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const char *text = Game::Lua::ToString(L, 2);
    // A character name can't hold '-', so the first one starts the realm.
    const char *dash = text != nullptr ? std::strchr(text, '-') : nullptr;
    const std::string name = dash != nullptr ? std::string(text, dash) : std::string(text ? text : "");
    if (name.empty()) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const char *classToken = Game::Lua::ToString(L, 3);
    const uint32_t classID = DBC::ClassIdForToken(classToken);
    // Optional race + sex. Missing / non-string / unknown values
    // resolve to 0, which Remember() treats as "leave existing value
    // alone" — so a 3-arg call preserves any prior race/sex data.
    uint32_t raceID = 0;
    if (Game::Lua::IsString(L, 4))
        raceID = DBC::RaceIdForToken(Game::Lua::ToString(L, 4));
    uint32_t sex = 0;
    if (Game::Lua::IsNumber(L, 5)) {
        const double raw = Game::Lua::ToNumber(L, 5);
        if (raw >= 0 && raw <= static_cast<double>(kMaxSex))
            sex = static_cast<uint32_t>(raw);
    }
    // Remember treats sex==0 as "leave alone" since male and "unknown"
    // share the same wire value. To flip a wrongly-cached female back
    // to male, the SMSG hook path (which does direct assignment) is
    // the route — RememberPlayer is for additive updates from addon
    // sources that may or may not know sex.
    Remember(guid, name.c_str(), dash != nullptr ? dash + 1 : "", classID, raceID, sex);
    Game::Lua::PushBoolean(L, 1);
    return 1;
}

// `C_PlayerCache.SetEnabled(bool)` — opt-in toggle for the persistent
// name cache. Persists to the account-level settings file. Enabling
// loads any prior cache contents for the current realm and imports
// what the engine already resolved this session.
int __fastcall Script_C_PlayerCache_SetEnabled(void *L) {
    Settings::Account::EnsureLoaded();
    bool desired = false;
    if (Game::Lua::IsNumber(L, 1))
        desired = Game::Lua::ToNumber(L, 1) != 0;
    else
        desired = Game::Lua::ToBoolean(L, 1) != 0;
    SetEnabled(desired);
    return 0;
}

int __fastcall Script_C_PlayerCache_IsEnabled(void *L) {
    Settings::Account::EnsureLoaded();
    Game::Lua::PushBoolean(L, g_enabled);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PlayerCache", "RememberPlayer",
                                     &Script_C_PlayerCache_RememberPlayer);
    Game::Lua::RegisterTableFunction("C_PlayerCache", "SetEnabled",
                                     &Script_C_PlayerCache_SetEnabled);
    Game::Lua::RegisterTableFunction("C_PlayerCache", "IsEnabled",
                                     &Script_C_PlayerCache_IsEnabled);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

const Entry *Lookup(uint64_t guid) {
    if (!IsEnabled())
        return nullptr;
    LoadCacheIfNeeded();
    auto it = g_entries.find(guid);
    return it != g_entries.end() ? &it->second : nullptr;
}

uint64_t GuidForName(const std::string &fullName) {
    if (fullName.empty() || !IsEnabled())
        return 0;
    LoadCacheIfNeeded();
    auto it = g_byName.find(fullName);
    return it != g_byName.end() ? it->second : 0;
}

void Remember(uint64_t guid, const char *name, const char *realm, uint32_t classID,
              uint32_t raceID, uint32_t sex) {
    if (guid == 0 || name == nullptr || name[0] == '\0' || !IsEnabled())
        return;
    LoadCacheIfNeeded();
    if (classID > kMaxClassID)
        classID = 0;
    if (raceID > kMaxRaceID)
        raceID = 0;
    if (sex > kMaxSex)
        sex = 0;
    if (Upsert(guid, SanitizeName(name), SanitizeRealm(realm), classID, raceID, sex)) {
        g_dirty = true;
        MaybeFlush();
    }
}

// Every gate reads the toggle through here, never `g_enabled` directly:
// nothing else guarantees the account settings are loaded before the
// first name response arrives (only optional glue Lua calls into the
// registry at character select).
bool IsEnabled() {
    Settings::Account::EnsureLoaded();
    return g_enabled;
}

void SetEnabled(bool enabled) {
    Settings::Account::EnsureLoaded();
    if (g_enabled == enabled)
        return;
    g_enabled = enabled;
    Settings::Account::Save();
    if (g_enabled) {
        LoadCacheIfNeeded();
        ImportEngineCache();
    } else if (g_dirty) {
        SaveCache();
    }
}

void Flush() {
    if (!g_enabled || !g_dirty)
        return;
    // SaveCache writes to the file the store was loaded from — returns
    // silently if nothing is loaded yet (pre-login).
    SaveCache();
}

} // namespace Player::NameCache
