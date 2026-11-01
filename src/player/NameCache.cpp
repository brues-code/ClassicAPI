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
//       has elapsed since the prior save, OR (c) `Flush()` is called
//       (/reload, /logout, process exit).
//
// One copy per player. The engine's name cache (`VAR_PLAYER_NAME_CACHE`)
// holds every player it named since the last world entry; this store holds
// the rest, and never a player the engine holds:
//   - SMSG_NAME_QUERY_RESPONSE hands a player to the engine. Every name the
//     engine learns arrives this way (chat, group, guild, every visible
//     player). We drop our entry just before the engine's handler stores its
//     record.
//   - SMSG_INVALIDATE_PLAYER (a rename) drops our entry; the engine frees
//     its own record.
//   - The engine empties its cache at each world entry
//     (`FUN_NAME_CACHE_CLEAR`) and frees it at exit (its atexit handler).
//     Both times we copy its records into this store first.
// A save writes both: this store, then the engine's records. Readers ask
// the engine first (`Player::Info`), so they find a player either way.
//
// `C_PlayerCache.RememberPlayer(guid, name, classToken)` adds players from
// sources the engine never names (e.g. /who results parsed via GetWhoInfo).
// Turning the cache on mid-session drops our entry for every player the
// engine already holds, as their replies would have.
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
#include "net/PacketDispatch.h"
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
// `FrameScript_Initialize` hook (covers /reload and /logout) and our
// atexit handler (`OnProcessExit`, a clean /quit) are the primary save
// path. The `MaybeFlush` on each change runs only as a defensive measure
// for long sessions that never reload AND end via hard process kill
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
// Set once the process is exiting (`Shutdown`). The engine's exit handler is
// about to free its name cache, or a fatal exit skipped that handler and left
// the cache in a crashing process. Nothing walks the cache after that.
bool g_engineFreed = false;
bool g_exitSaveRegistered = false;

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

// The entry for a name record, or false for one we don't keep: no name, or a
// class outside ChrClasses.dbc. An out-of-range race or sex means the record
// didn't carry it, so it becomes 0 ("unknown").
bool MakeEntry(const char *name, const char *realm, uint32_t classID, uint32_t raceID,
               uint32_t sex, Entry &out) {
    if (name == nullptr || classID == 0 || classID > kMaxClassID)
        return false;
    out.name = SanitizeName(name);
    if (out.name.empty())
        return false;
    out.realm = SanitizeRealm(realm);
    out.classID = classID;
    out.raceID = raceID <= kMaxRaceID ? raceID : 0;
    out.sex = sex <= kMaxSex ? sex : 0;
    return true;
}

// The entry for one of the engine's records (`OFF_PLAYER_INFO_*` layout).
bool RecordEntry(const uint8_t *record, Entry &out) {
    return MakeEntry(
        reinterpret_cast<const char *>(record + Offsets::OFF_PLAYER_INFO_NAME),
        reinterpret_cast<const char *>(record + Offsets::OFF_PLAYER_INFO_REALM),
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_PLAYER_INFO_CLASS),
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_PLAYER_INFO_RACE),
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_PLAYER_INFO_SEX), out);
}

// Drops the entry for `guid`. Returns true if there was one.
bool Erase(uint64_t guid) {
    auto it = g_entries.find(guid);
    if (it == g_entries.end())
        return false;
    g_byName.erase(FullName::Make(it->second.name.c_str(), it->second.realm.c_str()));
    g_entries.erase(it);
    return true;
}

// Drops our entries for a player the engine now holds as `e`: the one under
// its GUID, and the one under its full name, whose character was deleted or
// renamed since (names are unique per realm).
void HandOver(uint64_t guid, const Entry &e) {
    Erase(guid);
    auto it = g_byName.find(FullName::Make(e.name.c_str(), e.realm.c_str()));
    if (it != g_byName.end()) {
        const uint64_t other = it->second;
        Erase(other);
    }
}

// Calls `visit(guid, entry)` for each of the engine's records we keep
// (`MakeEntry`), and for none once the engine has freed its cache.
template <typename Visit>
void ForEachEngineEntry(Visit &visit) {
    if (g_engineFreed)
        return;
    Unit::Identity::ForEachCachedPlayer(
        [](uint64_t guid, const uint8_t *record, void *context) {
            Entry e;
            if (RecordEntry(record, e))
                (*static_cast<Visit *>(context))(guid, e);
            return true;
        },
        &visit);
}

// Copies the engine's records into this store just before the engine drops
// them. The saved file needs no change: a save writes the engine's records
// after ours, so loading it applies them the same way.
void TakeEngineRecords() {
    auto take = [](uint64_t guid, const Entry &e) {
        Upsert(guid, e.name, e.realm, e.classID, e.raceID, e.sex);
    };
    ForEachEngineEntry(take);
}

// Drops our entry for every player the engine already holds, for a cache
// turned on mid-session: their replies arrived while it was off. The file
// doesn't have their records yet, so the store turns dirty.
void HandOverAll() {
    auto handOver = [](uint64_t guid, const Entry &e) {
        HandOver(guid, e);
        g_dirty = true;
    };
    ForEachEngineEntry(handOver);
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

// One cache line. GUID first since it's the key. Variable-width hex — 1-8
// digits for player GUIDs (the high dword is always 0), up to 16 if a
// non-player GUID ever lands here.
void WriteLine(std::ostream &out, uint64_t guid, const Entry &e) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%llX\t", static_cast<unsigned long long>(guid));
    out << buf << e.name << '\t' << e.realm;
    std::snprintf(buf, sizeof(buf), "\t%u\t%u\t%u\n", e.classID, e.raceID, e.sex);
    out << buf;
}

// Writes this store, then the engine's records.
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
        out << "# ClassicAPI name cache v5 (per-realm, shared across accounts)\n";
        out << "# guid\tname\trealm\tclassID\traceID\tsex\n";
        for (const auto &kv : g_entries)
            WriteLine(out, kv.first, kv.second);
        auto write = [&out](uint64_t guid, const Entry &e) { WriteLine(out, guid, e); };
        ForEachEngineEntry(write);
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

// ----- engine name cache hand-off -----

// Name replies and renames, read before the engine's handler runs
// (`Net::PacketDispatch`), so the engine's record is still the old one. The
// backstop save goes first: a save after the drop would miss this player,
// who is in neither copy until the handler stores the record.
void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::SMSG_NAME_QUERY_RESPONSE &&
        opcode != Offsets::SMSG_INVALIDATE_PLAYER)
        return;
    if (!IsEnabled())
        return;
    const uint64_t guid = Net::Read<uint64_t>(packet);
    if (guid == 0)
        return;
    LoadCacheIfNeeded();

    if (opcode == Offsets::SMSG_INVALIDATE_PLAYER) {
        // The engine is about to free its record, so the next save leaves
        // the player out either way.
        const bool engineHeld = Unit::Identity::PlayerInfoRecord(guid) != nullptr;
        MaybeFlush();
        if (Erase(guid) || engineHeld)
            g_dirty = true;
        return;
    }

    // Read as HandleNameQueryResponse does (see SMSG_NAME_QUERY_RESPONSE).
    const char *name = Net::ReadCString(packet, Offsets::PLAYER_INFO_NAME_SIZE);
    const char *realm = Net::ReadCString(packet, Offsets::PLAYER_INFO_REALM_SIZE);
    const uint32_t raceID = Net::Read<uint32_t>(packet);
    const uint32_t sex = Net::Read<uint32_t>(packet);
    const uint32_t classID = Net::Read<uint32_t>(packet);
    // An empty name refuses the GUID: the engine stores nothing, so ours stays.
    Entry e;
    if (!MakeEntry(name, realm != nullptr ? realm : "", classID, raceID, sex, e))
        return;
    MaybeFlush();
    HandOver(guid, e);
    g_dirty = true; // the engine's record is not in the file yet
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

// Our atexit handler. The engine's own frees its name cache (see
// `FUN_CRT_ATEXIT`); ours was registered after it, so it runs first.
void __cdecl OnProcessExit() {
    if (!g_loadedPath.empty() && IsEnabled())
        TakeEngineRecords();
    Shutdown();
}

// `FUN_NAME_CACHE_CLEAR`, `__thiscall(this = cache)`, rendered as
// `__fastcall(self, edx)`. Runs at each world entry, before the engine names
// anyone. The store still holds the realm the records came from: it moves to
// a new realm on its first use after this.
using NameCacheClear_t = void(__thiscall *)(void *self);
NameCacheClear_t NameCacheClear_o = nullptr;

void __fastcall NameCacheClear_h(void *self, void *edx) {
    (void)edx;
    // From the first world entry on, the engine holds players until exit.
    // The game is running by now, so the engine's startup registrations are
    // all older than ours.
    if (!g_exitSaveRegistered) {
        using Atexit_t = int(__cdecl *)(void(__cdecl *)());
        g_exitSaveRegistered = reinterpret_cast<Atexit_t>(
            static_cast<uintptr_t>(Offsets::FUN_CRT_ATEXIT))(&OnProcessExit) == 0;
    }
    if (!g_loadedPath.empty() && IsEnabled())
        TakeEngineRecords();
    NameCacheClear_o(self);
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_NAME_CACHE_CLEAR,
    reinterpret_cast<void *>(&NameCacheClear_h),
    reinterpret_cast<void **>(&NameCacheClear_o)};

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
    // share the same wire value. A wrongly-cached female goes back to
    // male through the server's name reply, which replaces our entry
    // with the engine's record — RememberPlayer is for additive updates
    // from addon sources that may or may not know sex.
    Remember(guid, name.c_str(), dash != nullptr ? dash + 1 : "", classID, raceID, sex);
    Game::Lua::PushBoolean(L, 1);
    return 1;
}

// `C_PlayerCache.SetEnabled(bool)` — opt-in toggle for the persistent
// name cache. Persists to the account-level settings file. Enabling
// loads any prior cache contents for the current realm and leaves the
// players the engine already named this session to the engine.
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
    // The engine's record is the server's own answer, and it is saved.
    Entry held;
    const uint8_t *record = Unit::Identity::PlayerInfoRecord(guid);
    if (record != nullptr && RecordEntry(record, held))
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
        HandOverAll();
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

void Shutdown() {
    g_engineFreed = true;
    Flush();
}

} // namespace Player::NameCache
