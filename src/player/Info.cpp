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

// `GetPlayerInfoByGUID(guid)` — modern signature returning
// `localizedClass, englishClass, localizedRace, englishRace, sex,
//  name, realm` for any player GUID the engine has cached. Vanilla
// 1.12 has the cache (populated by SMSG_NAME_QUERY_RESPONSE) but
// doesn't expose it to Lua.
//
// Cache lookup path: call `FUN_0055F080` on the cache instance at
// `VAR_PLAYER_NAME_CACHE` (0x00C0E228) with a NULL callback. The
// engine returns a pointer to the entry's data block if the GUID is
// loaded; NULL otherwise (miss, or pending request from prior
// engine-initiated query). We never call with a non-NULL callback
// from this read path — that would trigger CMSG_NAME_QUERY (opcode
// 0x50), which is the right behavior for an explicit
// `RequestLoadPlayerInfo` API but not for a passive getter that may
// be called many times per frame from tooltips/raid frames.
//
// Cache coverage: the engine populates entries for any GUID it
// receives a SMSG_NAME_QUERY_RESPONSE for, which is triggered by
// chat (whispers, says, etc.), raid/group events, guild updates,
// and visible-object resolution. So a friend you've never seen in
// chat won't be in the cache; once they whisper you (or you target
// them, or they appear in guild login spam), they will be.

#include "Game.h"
#include "Info.h"
#include "NameCache.h"
#include "Offsets.h"
#include "dbc/Names.h"
#include "friendlist/FriendList.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace Player::Info {

namespace {

// Bounded, always-terminated copy. Returns true iff a non-empty name landed.
bool CopyName(char *dst, const char *src, size_t n) {
    if (dst == nullptr || n == 0)
        return false;
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
    return dst[0] != '\0';
}

// Thin shim: parses a GUID string and splits to hi/lo dwords. Shared parser
// lives in `Guid::Parse`.
bool ParseGUID(const char *str, uint32_t &outHi, uint32_t &outLo) {
    uint64_t value = 0;
    if (!Guid::Parse(str, &value))
        return false;
    outHi = static_cast<uint32_t>(value >> 32);
    outLo = static_cast<uint32_t>(value);
    return true;
}

// Pushes `(localizedClass, englishClass, localizedRace, englishRace, sex,
// name, realm)` for a player GUID and returns 7, or pushes nothing and
// returns 0 if nothing knows that GUID. Engine name cache first, then the
// persistent NameCache (when enabled) — whose realm is always "", since
// vanilla is single-realm and never carries a per-player realm.
int PushPlayerInfo(void *L, uint64_t guid) {
    if (guid == 0)
        return 0;

    const char *name = nullptr;
    const char *realm = nullptr;
    uint32_t race = 0;
    uint32_t sex = 0;
    uint32_t classID = 0;

    if (const uint8_t *entry = Unit::Identity::PlayerInfoRecord(guid)) {
        name = reinterpret_cast<const char *>(
            entry + Offsets::OFF_PLAYER_INFO_NAME);
        realm = reinterpret_cast<const char *>(
            entry + Offsets::OFF_PLAYER_INFO_REALM);
        race = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_PLAYER_INFO_RACE);
        sex = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_PLAYER_INFO_SEX);
        classID = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_PLAYER_INFO_CLASS);
    } else {
        const std::string *cachedName = nullptr;
        const NameCache::Entry *cached = NameCache::Lookup(guid, &cachedName);
        if (cached == nullptr || cachedName == nullptr || cachedName->empty())
            return 0;
        name = cachedName->c_str();
        classID = cached->classID;
        race = cached->raceID;
        sex = cached->sex;
        realm = "";
    }

    const char *englishClass = DBC::ClassToken(classID);
    const char *localizedClass = DBC::ClassName(classID);
    const char *englishRace = DBC::RaceToken(race);
    const char *localizedRace = DBC::RaceName(race);

    Game::Lua::PushString(L, localizedClass ? localizedClass : "");
    Game::Lua::PushString(L, englishClass ? englishClass : "");
    Game::Lua::PushString(L, localizedRace ? localizedRace : "");
    Game::Lua::PushString(L, englishRace ? englishRace : "");
    // Match modern UnitSex convention: 2 = male, 3 = female. The
    // wire format and cache use 0/1 (per CMaNGOS GENDER_MALE/
    // GENDER_FEMALE); modern WoW remaps for the Lua surface.
    Game::Lua::PushNumber(L, static_cast<double>(sex + 2));
    Game::Lua::PushString(L, name);
    Game::Lua::PushString(L, realm);
    return 7;
}

int __fastcall Script_GetPlayerInfoByGUID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: GetPlayerInfoByGUID(\"0x...\")");
        return 0;
    }
    uint32_t hi, lo;
    if (!ParseGUID(Game::Lua::ToString(L, 1), hi, lo))
        return 0;
    return PushPlayerInfo(L, (static_cast<uint64_t>(hi) << 32) | lo);
}

// `UnitNameFromGUID(guid)` → `(name, realm)`. Modern WoW backport;
// vanilla 1.12 doesn't expose any GUID → name accessor at the Lua
// surface, so addons that need to resolve a chat link or combat-log
// GUID can't get a name without round-tripping through a unit token.
//
// Resolution order (matches vanilla's own `Script_UnitName` at
// `0x00517020`):
//   1. Resolve the GUID through the object manager. Any currently-
//      synced unit — player you can see, NPC in interact range,
//      target / mouseover / party / raid member — gets a `CGObject *`
//      back. Call the engine's canonical polymorphic name getter
//      `FUN_OBJECT_GET_NAME` on it; this routes through the player
//      NameCache for players AND the creature cache for NPCs, so it
//      works for both with a single call.
//   2. Friends list. It keeps name + GUID for online AND offline
//      friends (SMSG_FRIEND_LIST), so a friend resolves even while
//      unsynced and never seen in chat. Always on (not opt-in).
//   3. If still unresolved (ex-target who logged off, player
//      encountered earlier in chat but no longer visible), fall back
//      to the persistent NameCache (when enabled).
//
// `realm` is always `""` in vanilla — the engine doesn't populate
// per-player realm names and 1.12 has no cross-realm interaction.
// We push the empty string rather than nil so callers don't have to
// special-case the second return; modern WoW returns nil, but
// `realm == ""` is just as easy to gate on.
//
// Returns nothing on:
//   - Missing or non-string arg (raises an error rather than return).
//   - Unparseable GUID string.
//   - Zero / NULL GUID.
//   - GUID not resolvable in the object manager, not on the friends
//     list, AND not in the persistent NameCache (never seen this unit).
//
// Doesn't trigger a network query on miss — passive read, same as
// `GetPlayerInfoByGUID`. Use `C_PlayerCache.RememberPlayer` or let
// the engine populate via normal interaction.
int __fastcall Script_UnitNameFromGUID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitNameFromGUID(\"0x...\")");
        return 0;
    }
    uint32_t hi, lo;
    if (!ParseGUID(Game::Lua::ToString(L, 1), hi, lo))
        return 0;
    char name[64];
    if (!NameFromGuid((static_cast<uint64_t>(hi) << 32) | lo, name, sizeof name))
        return 0;
    Game::Lua::PushString(L, name);
    Game::Lua::PushString(L, ""); // realm — always "" in vanilla
    return 2;
}

// `C_PlayerCache.GetPlayerInfoByName(name)` — returns `(localizedClass,
// englishClass, localizedRace, englishRace, sex, name, realm, guid)` or
// nothing if the name can't be resolved. Resolves the name with
// `GuidFromName`, then builds the same tuple as `GetPlayerInfoByGUID`, so a
// nearby or grouped player works whether or not the persistent cache is on.
//
// Companion to `GetPlayerInfoByGUID` for the case where an addon has
// a player's name from a `|Hplayer:Name|h` chat link or
// CHAT_MSG_SYSTEM string but `GetCurrentChatGUID()` returns nil (the
// engine doesn't propagate the sender GUID for every chat path).
// The 8th `guid` return — absent from `GetPlayerInfoByGUID` since
// the caller already has it — lets name-based callers chain into
// any GUID-keyed API (e.g., friend list, ignore list).
int __fastcall Script_C_PlayerCache_GetPlayerInfoByName(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const uint64_t guid = GuidFromName(Game::Lua::ToString(L, 1));
    if (PushPlayerInfo(L, guid) == 0)
        return 0;
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(guid, buf, sizeof buf));
    return 8;
}

} // namespace

// Resolution order matches vanilla's own `Script_UnitName` (0x00517020):
//   1. Object manager — TYPEMASK_OBJECT (0x01) is permissive; the name getter
//      at `FUN_OBJECT_GET_NAME` does its own type check and routes through the
//      player NameCache for players AND the creature cache for NPCs, so it
//      resolves both with a single call. It returns the "UNKNOWNOBJECT"
//      sentinel for non-unit objects (gameobjects etc.), so we gate on that
//      rather than pre-filtering by typemask.
//   2. Friends list — keeps name + GUID for online AND offline friends, so a
//      friend resolves even while unsynced and never seen in chat.
//   3. Persistent NameCache (when enabled) — ex-units no longer in the sync
//      window (an ex-target who logged off, a chat name no longer visible).
bool NameFromGuid(uint64_t guid, char *buf, size_t bufSize) {
    if (guid == 0 || buf == nullptr || bufSize == 0)
        return false;

    using GetName_t = const char *(__thiscall *)(void *obj, int *outFlags);
    if (void *obj = Object::ByGuid(Offsets::TYPEMASK_OBJECT, guid, nullptr, 0)) {
        auto getName = reinterpret_cast<GetName_t>(Offsets::FUN_OBJECT_GET_NAME);
        const char *name = getName(obj, nullptr);
        if (name != nullptr && *name != '\0' &&
            std::strcmp(name, "UNKNOWNOBJECT") != 0 &&
            std::strcmp(name, "Unknown Being") != 0)
            return CopyName(buf, name, bufSize);
    }

    if (const char *fname = FriendList::NameForGuid(guid))
        return CopyName(buf, fname, bufSize);

    const std::string *cachedName = nullptr;
    const NameCache::Entry *cached = NameCache::Lookup(guid, &cachedName);
    if (cached != nullptr && cachedName != nullptr && !cachedName->empty())
        return CopyName(buf, cachedName->c_str(), bufSize);

    return false;
}

// Resolution order, most live first:
//   1. The engine's by-name search (`Unit::Identity::FindGuidByName`,
//      players only, whole-name match) — party and raid members, including
//      out-of-range ones, then every visible player.
//   2. Friends list — online and offline friends.
//   3. Persistent NameCache (when enabled) — players seen earlier.
uint64_t GuidFromName(const char *name) {
    if (name == nullptr || *name == '\0')
        return 0;
    if (const uint64_t guid = Unit::Identity::FindGuidByName(
            name, Offsets::TYPEMASK_PLAYER, /*exact*/ true))
        return guid;
    if (const uint64_t guid = FriendList::GuidForName(name))
        return guid;
    if (const NameCache::Entry *e = NameCache::LookupByName(name))
        return e->guid;
    return 0;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetPlayerInfoByGUID",
                                       &Script_GetPlayerInfoByGUID);
    Game::Lua::RegisterGlobalFunction("UnitNameFromGUID",
                                       &Script_UnitNameFromGUID);
    Game::Lua::RegisterTableFunction("C_PlayerCache", "GetPlayerInfoByName",
                                     &Script_C_PlayerCache_GetPlayerInfoByName);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Player::Info
