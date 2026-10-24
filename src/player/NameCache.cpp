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
//   `WTF\ClassicAPI\<realmList>\<realm>\ClassicAPI_NameCache.txt`
//       Per-realm cache contents, SHARED across every account on the
//       same server. Keyed by realmlist address (unambiguous server
//       identity — can't collide across unrelated private servers) plus
//       the realm name, rather than by account, so alts on different
//       accounts pool into one file. Not under `Account\`, so we create
//       the directory tree ourselves. v4 format. One entry per line:
//           `name  guid  classID  raceID  sex`
//       (fields tab-separated). Loaded once the realm name is
//       available post-login (migrating a pre-existing per-account file
//       in on first load). Saved when (a) a recent write hasn't been
//       flushed AND (b) at least 30 seconds have elapsed since the prior
//       save, OR (c) DLL teardown / `Flush()` is called.
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
// Lookups are GUID-keyed. Names ARE the dedup signal though: vanilla
// server-side names are unique per realm, so a fresh GUID claiming a
// name we've already seen means the prior character was deleted and
// the name recycled. The old GUID is dead (server won't honor it
// again); keeping it just bloats the file. `EvictStaleGuidsForName`
// drops the old entry on each write so the cache holds one entry per
// name.

#include "NameCache.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"
#include "guid/Guid.h"
#include "settings/Account.h"
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

// Name-primary storage. Vanilla's per-realm name uniqueness makes
// name the natural identity key; deletion-recreation produces a new
// GUID for the same name, which inserts-replaces here instead of
// piling up dead entries the way a GUID-keyed map would.
//
// Keys compare ignoring ASCII case, the same rule as the engine's own
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
std::unordered_map<std::string, Entry, NameHash, NameEqual> g_entries;
// `guid → name` reverse index. Maintained in lockstep with
// `g_entries`. Invariant: `g_guidIndex[guid] == name` iff
// `g_entries[name].guid == guid`.
std::unordered_map<uint64_t, std::string> g_guidIndex;
bool g_enabled = false;
bool g_cacheLoaded = false;
bool g_dirty = false;
DWORD g_lastSaveTick = 0;

// ----- engine globals helpers (mirrors EquipmentSet::Storage) -----

const char *ReadAccountName() {
    return *reinterpret_cast<const char *const *>(
        static_cast<uintptr_t>(Offsets::VAR_ACCOUNT_NAME_PTR));
}

const char *ReadRealmName() {
    auto *info = *reinterpret_cast<uint8_t **>(
        static_cast<uintptr_t>(Offsets::VAR_REALM_INFO_PTR));
    if (info == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(
        info + Offsets::OFF_REALM_INFO_NAME);
}

// ----- path resolution -----

// realmList CVar value (the realmlist address, e.g.
// "logon.turtle-wow.org") — the unambiguous server identity. Unlike the
// friendly realm name it can't collide across unrelated private servers,
// so it's the outer key for the shared cache path. Mirrors
// `Security::CredStore`'s realm scoping.
const char *ReadRealmList() {
    using GetRealmList_t = const char *(__fastcall *)(int forceRefresh);
    return reinterpret_cast<GetRealmList_t>(
        static_cast<uintptr_t>(Offsets::FUN_CVAR_REALM_LIST))(0);
}

// Filesystem-safe path component. We create these directories ourselves
// (WoW doesn't), so any character Windows rejects in a path — or that
// could surprise, like the space/apostrophe in "N'Zoth" — becomes '_'.
// Deterministic, so a realm always maps to the same folder.
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
//   WTF\ClassicAPI\<realmList>\<realmName>\ClassicAPI_NameCache.txt
// Not under Account\, so alts on different accounts of the same server
// share one pool. Empty (defer) until both realmlist + realm are known.
std::string CachePath() {
    const char *realmList = ReadRealmList();
    if (realmList == nullptr || realmList[0] == '\0')
        return {};
    const char *realm = ReadRealmName();
    if (realm == nullptr || realm[0] == '\0')
        return {};
    std::string out = "WTF\\ClassicAPI\\";
    out += SanitizeComponent(realmList);
    out += '\\';
    out += SanitizeComponent(realm);
    out += "\\ClassicAPI_NameCache.txt";
    return out;
}

// The pre-shared per-account path, used only to migrate an existing
// cache into the shared location on first load. Built exactly as it was
// written (raw account + realm, unsanitized) so it matches the file
// sitting in WoW's own folders.
std::string OldCachePath() {
    const char *account = ReadAccountName();
    if (account == nullptr || account[0] == '\0')
        return {};
    const char *realm = ReadRealmName();
    if (realm == nullptr || realm[0] == '\0')
        return {};
    std::string out = "WTF\\Account\\";
    out += account;
    out += '\\';
    out += realm;
    out += "\\ClassicAPI_NameCache.txt";
    return out;
}

// Creates every parent directory of `filePath` (mkdir -p). WoW makes
// `WTF\` but not `WTF\ClassicAPI\...`, and ofstream won't create
// directories, so a save into a fresh realm folder would silently fail
// without this. CreateDirectory on an existing dir no-ops.
void EnsureParentDirs(const std::string &filePath) {
    for (size_t pos = 0;;) {
        const size_t slash = filePath.find('\\', pos);
        if (slash == std::string::npos)
            break; // remainder is the filename
        if (slash > 0)
            CreateDirectoryA(filePath.substr(0, slash).c_str(), nullptr);
        pos = slash + 1;
    }
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

// Lightweight name sanitizer — vanilla player names are 2..12 ASCII
// letters, but for paranoia drop anything that would break a line
// (tab, newline) and cap length. Anything else passes through.
std::string SanitizeName(const char *name) {
    if (name == nullptr)
        return {};
    std::string out;
    for (const char *p = name; *p && out.size() < 16; ++p) {
        const char c = *p;
        if (c == '\t' || c == '\r' || c == '\n')
            continue;
        out.push_back(c);
    }
    return out;
}

// Inserts or updates an entry keyed by `name`, maintaining the
// `g_guidIndex` reverse-map invariant. Handles three eviction cases:
//
//   1. A prior entry exists under this name with a different GUID
//      (name recycled — character deleted and recreated). Drop the
//      old entry and its guid-index entry.
//   2. A prior entry exists under this GUID with a different name
//      (character renamed — rare in vanilla but defensible). Drop
//      the old name entry and its guid-index entry.
//   3. Same name + same GUID — update in place.
//
// `classID` / `raceID` / `sex` of 0 mean "caller doesn't know" and
// don't overwrite an existing non-zero field. Returns true if any
// change occurred (caller marks the cache dirty for flushing).
bool Upsert(uint64_t guid, const std::string &name,
            uint32_t classID, uint32_t raceID, uint32_t sex) {
    if (name.empty() || guid == 0)
        return false;
    bool changed = false;

    // Case 1: same name under a different GUID — recycled name.
    auto nameIt = g_entries.find(name);
    if (nameIt != g_entries.end() && nameIt->second.guid != guid) {
        g_guidIndex.erase(nameIt->second.guid);
        g_entries.erase(nameIt);
        changed = true;
    }

    // Case 2: same GUID under a different name — rename.
    auto guidIt = g_guidIndex.find(guid);
    if (guidIt != g_guidIndex.end() && guidIt->second != name) {
        g_entries.erase(guidIt->second);
        g_guidIndex.erase(guidIt);
        changed = true;
    }

    // Case 3: insert or update in place.
    Entry &e = g_entries[name];
    if (e.guid != guid) {
        e.guid = guid;
        changed = true;
    }
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
    g_guidIndex[guid] = name;
    return changed;
}

// Parses v4 cache lines (`name\tguid\tclass\trace\tsex`) from `file`
// into the store. Name first since it's the primary key; GUID is a
// peripheral field carried for GUID-keyed lookups. Variable-width hex
// GUID (no leading zeros). Shared by the normal load and the one-time
// migration from the old per-account file.
void LoadFromStream(std::istream &file) {
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream is(line);
        std::string name, guidStr, classStr, raceStr, sexStr;
        if (!(is >> name >> guidStr >> classStr >> raceStr >> sexStr))
            continue;

        const std::string sanitized = SanitizeName(name.c_str());
        const uint64_t guid = std::strtoull(guidStr.c_str(), nullptr, 16);
        const uint32_t classID = static_cast<uint32_t>(std::strtoul(classStr.c_str(), nullptr, 10));
        const uint32_t raceID = static_cast<uint32_t>(std::strtoul(raceStr.c_str(), nullptr, 10));
        const uint32_t sex = static_cast<uint32_t>(std::strtoul(sexStr.c_str(), nullptr, 10));
        if (guid == 0 || sanitized.empty())
            continue;
        if (classID > kMaxClassID || raceID > kMaxRaceID || sex > kMaxSex)
            continue;
        // Upsert handles the rare same-name / same-guid dedup if the
        // file has duplicate rows (shouldn't, but defensive).
        Upsert(guid, sanitized, classID, raceID, sex);
    }
}

void LoadCacheIfNeeded() {
    if (g_cacheLoaded)
        return;
    const std::string path = CachePath();
    if (path.empty())
        return; // pre-realm; retry on next call (g_cacheLoaded stays false)
    g_cacheLoaded = true;

    std::ifstream file(path);
    if (file.is_open()) {
        LoadFromStream(file);
        return;
    }

    // No shared file yet. One-time migration: seed from the old
    // per-account file if it exists, then mark dirty so the next save
    // writes the merged data into the shared path. Only when the shared
    // file is absent — once it exists it's authoritative (a second
    // account's old-only entries just repopulate via scan / SMSG).
    const std::string oldPath = OldCachePath();
    if (oldPath.empty())
        return;
    std::ifstream oldFile(oldPath);
    if (oldFile.is_open()) {
        LoadFromStream(oldFile);
        g_dirty = true;
    }
}

void SaveCache() {
    const std::string path = CachePath();
    if (path.empty())
        return;
    // `WTF\ClassicAPI\<realmList>\<realm>\` isn't created by WoW — make
    // the tree first, else the ofstream open below silently fails.
    EnsureParentDirs(path);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return;
        // Single hex GUID column with no leading zeros. Player GUIDs
        // (all we cache today) write as 1-8 hex digits since the high
        // dword is always 0; non-player GUIDs would naturally extend
        // to up to 16 digits if any ever landed here.
        // Name-primary v4 format. Name first since it's the primary
        // identity (server-enforced unique per realm) and the natural
        // sort/scan key when hand-editing. GUID is variable-width
        // hex — 1-8 digits for player GUIDs, up to 16 if a non-player
        // GUID ever lands here.
        out << "# ClassicAPI name cache v4 (per-realm, shared across accounts)\n";
        out << "# name\tguid\tclassID\traceID\tsex\n";
        for (const auto &kv : g_entries) {
            const std::string &name = kv.first;
            const Entry &e = kv.second;
            char buf[80];
            std::snprintf(buf, sizeof(buf), "\t%llX\t%u\t%u\t%u",
                          static_cast<unsigned long long>(e.guid),
                          e.classID, e.raceID, e.sex);
            out << name << buf << "\n";
        }
        if (!out)
            return;
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
    g_dirty = false;
    g_lastSaveTick = GetTickCount();
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
    const std::string sanitized = SanitizeName(name);
    // Out-of-range race/sex mean the record didn't carry them (some
    // entry types skip these fields); pass 0 so `Upsert` preserves
    // any prior real data.
    const uint32_t safeRace = (raceID != 0 && raceID <= kMaxRaceID) ? raceID : 0;
    const uint32_t safeSex = (sex <= kMaxSex) ? sex : 0;
    if (Upsert(guid, sanitized, classID, safeRace, safeSex)) {
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
// [, sex]])` — stores the entry in the persistent cache. Class and
// race tokens are uppercase engine tokens (`"WARRIOR"`, `"NIGHTELF"`,
// etc.); sex is `0` (male) or `1` (female), matching the wire-format
// convention the engine cache stores. Trailing args are optional —
// passing only the first three preserves the old 3-arg signature.
// Returns `true` on success, `false` if the cache is disabled or the
// required args are malformed.
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
    const char *name = Game::Lua::ToString(L, 2);
    if (name == nullptr || name[0] == '\0') {
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
    Remember(guid, name, classID, raceID, sex);
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

const Entry *Lookup(uint64_t guid, const std::string **outName) {
    if (outName != nullptr)
        *outName = nullptr;
    if (!IsEnabled())
        return nullptr;
    LoadCacheIfNeeded();
    auto guidIt = g_guidIndex.find(guid);
    if (guidIt == g_guidIndex.end())
        return nullptr;
    auto entryIt = g_entries.find(guidIt->second);
    if (entryIt == g_entries.end())
        return nullptr; // shouldn't happen if invariant holds
    if (outName != nullptr)
        *outName = &entryIt->first;
    return &entryIt->second;
}

const Entry *LookupByName(const char *name) {
    if (name == nullptr || name[0] == '\0' || !IsEnabled())
        return nullptr;
    LoadCacheIfNeeded();
    auto it = g_entries.find(name);
    if (it == g_entries.end())
        return nullptr;
    return &it->second;
}

void Remember(uint64_t guid, const char *name, uint32_t classID,
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
    const std::string sanitized = SanitizeName(name);
    if (Upsert(guid, sanitized, classID, raceID, sex)) {
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
    // SaveCache resolves the path itself — returns silently if realm
    // name isn't available yet (pre-login).
    SaveCache();
}

} // namespace Player::NameCache
