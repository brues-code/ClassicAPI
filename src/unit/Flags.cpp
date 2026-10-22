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

#include "Flags.h"

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Unit::Flags {

bool IsPlayerControlled(const uint8_t *unit) {
    if (unit == nullptr)
        return false;
    auto *fields =
        Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr)
        return false;
    const uint32_t unitFlags =
        Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);
    return (unitFlags & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) != 0;
}

bool IsPlayerObject(const uint8_t *unit) {
    if (unit == nullptr)
        return false;
    return Game::Read<int>(unit, Offsets::OFF_CGOBJECT_TYPE_ID) ==
           Offsets::OBJECT_TYPE_PLAYER;
}

namespace {

// Reads a PLAYER_FLAGS bit for a player. Path:
//   - Resolve unit; gate on the object actually being a player
//     (`OBJECT_TYPE_PLAYER`) so we never read the +0xE68 CGPlayer
//     sub-struct on a non-player object. UNIT_FLAG_PLAYER_CONTROLLED is
//     insufficient — pets/totems/MC'd creatures set it but have no
//     sub-struct there.
//   - Read `[unit + 0xE68] + 0x08` — the unit's CGPlayer-side info
//     struct, where byte +0x08 is PLAYER_FLAGS. Same struct
//     `Script_IsResting` reads at +0x05 bit (RESTING = 0x20) for the
//     local player; same struct the nameplate AFK-rendering code at
//     `0x005EC9E0` reads at bit 1 (AFK = 0x02) for **any** unit being
//     rendered. PLAYER_FLAGS lives here for all players — local and
//     remote — even though it's not in the broadcast UpdateField data.
//
// The `[unit + 0xE68]` pointer is the same one the visible-items
// helper (`FUN_UNIT_GET_VISIBLE_ITEM`) walks at offset `+0x118 +
// slot*0x30`; it's a CGPlayer-specific sub-struct that's allocated
// for any player-controlled unit, with PLAYER_FLAGS at +0x08 and
// visible-items table at +0x118.
bool TestPlayerFlag(void *L, uint32_t flagMask) {
    if (!Game::Lua::IsString(L, 1))
        return false;
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr)
        return false;

    auto *unit = static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
    // PLAYER_FLAGS lives in the CGPlayer sub-struct — gate on the object
    // actually being a player, not merely player-controlled (pets/totems
    // set that flag but have no sub-struct → garbage +0xE68 deref).
    if (!IsPlayerObject(unit))
        return false;

    auto *playerInfo =
        Game::Read<const uint8_t *>(unit, Offsets::OFF_CGPLAYER_INFO);
    if (playerInfo == nullptr)
        return false;
    const uint32_t flags =
        Game::Read<uint32_t>(playerInfo, Offsets::OFF_PLAYER_INFO_FLAGS);
    return (flags & flagMask) != 0;
}

// `UnitIsAFK(unit)` — returns true if the unit is currently AFK
// (toggled via `/afk` or auto-set after idle timeout). Works for any
// player-controlled unit (player, target, party*, raid*, etc.). NPCs
// always return false.
int __fastcall Script_UnitIsAFK(void *L) {
    Game::Lua::PushBool(L, TestPlayerFlag(L, Offsets::PLAYER_FLAG_AFK));
    return 1;
}

// `UnitIsDND(unit)` — returns true if the unit is currently DND
// ("Do Not Disturb", toggled via `/dnd`). Same unit-token coverage as
// `UnitIsAFK`.
int __fastcall Script_UnitIsDND(void *L) {
    Game::Lua::PushBool(L, TestPlayerFlag(L, Offsets::PLAYER_FLAG_DND));
    return 1;
}

// `UnitIsFeignDeath(unit)` — returns true if the unit is feigning
// death (Hunter's `Feign Death`). Reads `UNIT_FIELD_FLAGS` bit 29
// (`0x20000000`) directly off the unit's m_objectFields descriptor.
// Unlike AFK/DND, this works for any unit token because
// UNIT_FIELD_FLAGS is broadcast in object updates — anyone watching
// the hunter sees the flag set.
int __fastcall Script_UnitIsFeignDeath(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitIsFeignDeath(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }

    auto *unit = static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
    if (unit == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    auto *fields =
        Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const uint32_t unitFlags =
        Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);
    Game::Lua::PushBoolean(L,
        (unitFlags & Offsets::UNIT_FLAG_FEIGN_DEATH) != 0);
    return 1;
}

// Reads `[unit + 0xE68 + 0x0C]` (the CGPlayer sub-struct's guild-key
// field) with all the safety gates needed for arbitrary unit tokens.
// Returns 0 if the unit isn't player-controlled, isn't a player, or
// hasn't had its guild data synced.
uint32_t ReadGuildKey(const uint8_t *unit) {
    // The guild key lives in the CGPlayer sub-struct at +0xE68, which
    // only exists on real player objects. A player-controlled *creature*
    // (pet, totem, MC'd mob) has UNIT_FLAG_PLAYER_CONTROLLED set but no
    // sub-struct, so gating on IsPlayerControlled here would deref garbage
    // and crash (issue: UnitIsInMyGuild on a nameplate pet/totem).
    if (!IsPlayerObject(unit))
        return 0;
    auto *info = Game::Read<const uint8_t *>(unit, Offsets::OFF_CGPLAYER_INFO);
    if (info == nullptr)
        return 0;
    return Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_GUILD_KEY);
}

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);
// Storm SStrCmpI — `__stdcall`, RET 0xC (see Offsets.h).
using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);
using PlayerInfoLookup_t = const uint8_t *(__thiscall *)(
    void *cache, uint32_t guidLo, uint32_t guidHi, uint64_t *cookie,
    void *callback, void *userData, int retryFlag);

// Resolve a GUID to its live CGPlayer object, or nullptr if no player
// object with that GUID is loaded. Never raises.
const uint8_t *ResolvePlayerByGuid(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    return static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_PLAYER, guid, "UnitIsInMyGuild", 0));
}

// Resolves a player GUID to its character name via
// `FUN_PLAYER_INFO_LOOKUP` with a NULL callback — a pure NameCache
// read that returns the entry block for any GUID the engine has seen a
// `SMSG_NAME_QUERY_RESPONSE` for (same path `GetPlayerInfoByGUID`
// uses). This is the OOR-safe fallback: it works for a raid member in
// another zone whose CGUnit isn't loaded.
//
// Returns `nameBuf` on success (copied out — the entry's name pointer
// isn't guaranteed stable across reentrant cache writes), or nullptr
// when the GUID isn't a player the engine has name-queried (NPC GUID,
// or not yet queried).
const char *NameFromGuid(uint64_t guid, char (&nameBuf)[64]) {
    if (guid == 0)
        return nullptr;

    auto lookup = reinterpret_cast<PlayerInfoLookup_t>(
        Offsets::FUN_PLAYER_INFO_LOOKUP);
    auto *cache = reinterpret_cast<void *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_NAME_CACHE));
    uint64_t cookie = 0;
    const uint8_t *entry = lookup(cache,
                                  static_cast<uint32_t>(guid),
                                  static_cast<uint32_t>(guid >> 32),
                                  &cookie, nullptr, nullptr, 0);
    if (entry == nullptr)
        return nullptr;

    std::snprintf(nameBuf, sizeof(nameBuf), "%s",
                  Game::Ptr<const char>(entry, Offsets::OFF_PLAYER_INFO_NAME));
    return nameBuf;
}

// `UnitIsInMyGuild(unitOrName)` — returns `1` if the unit/character
// shares the player's guild, `nil` otherwise. Accepts a character name
// (e.g. `"Bob"`) or a unit token (e.g. `"target"`, `"party1"`).
//
// Mirrors 4.3.4's `Script_UnitIsInMyGuild` step for step:
//
// 1. Name match: the engine's by-name search (`FUN_UNIT_FIND_BY_NAME`,
//    4.3.4's `ClosestObjectMatch(name, TYPEMASK_PLAYER, 3, exact=1)`):
//    party members, then every loaded player, whole name, ignoring case.
//    A loaded hit is decided on the guild key alone — no roster needed,
//    and no roster fallback (4.3.4 returns here either way).
//
// 2. Token: `FUN_TOKEN_TO_GUID` (the non-throwing resolver, which also
//    knows `focus` / `nameplateN`) turns a token into a GUID, and the
//    engine NameCache (`FUN_PLAYER_INFO_LOOKUP`, 4.3.4's
//    `g_nameDBCache.GetRecord`) turns that into a name. A loaded player's
//    name comes from the same cache. If either step finds nothing, the
//    input itself is the name, as in 4.3.4.
//
// 3. Roster: walk every guild member (`VAR_GUILD_ROSTER_TOTAL_COUNT`,
//    offline ones too) comparing names ignoring case. 4.3.4 uses
//    SStrCmpUTF8I; 1.12 has only SStrCmpI (ASCII folding). Needs
//    `GuildRoster()` to have filled the roster.
//
// One deliberate addition: when the player's own guild key is 0 (no
// guild), the answer is nil before any lookup, so a stale roster can't
// answer for a guild the player has left.
//
// Returns `1.0`/`nil`, never a boolean — 4.3.4 pushes
// `lua_pushnumber(1.0)` / `lua_pushnil`.
int __fastcall Script_UnitIsInMyGuild(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitIsInMyGuild(\"name\")");
        return 0;
    }
    const char *input = Game::Lua::ToString(L, 1);
    if (input == nullptr || *input == '\0') {
        Game::Lua::PushNil(L);
        return 1;
    }

    const uint32_t playerKey = ReadGuildKey(
        static_cast<const uint8_t *>(Game::ResolveUnitToken("player")));
    if (playerKey == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }

    // 1. A loaded player matched by name — decided on the guild key.
    constexpr int kPartyThenObjects = 3;
    const uint8_t *match = ResolvePlayerByGuid(Unit::Identity::FindGuidByName(
        input, Offsets::TYPEMASK_PLAYER, /*exact*/ true, kPartyThenObjects));
    if (match != nullptr) {
        if (ReadGuildKey(match) == playerKey)
            Game::Lua::PushNumber(L, 1.0);
        else
            Game::Lua::PushNil(L);
        return 1;
    }

    // 2. A unit token — its name, from the engine NameCache.
    const char *nameToFind = input;
    char nameBuf[64];
    auto toGuid = reinterpret_cast<TokenToGUID_t>(Offsets::FUN_TOKEN_TO_GUID);
    if (const char *tokenName = NameFromGuid(toGuid(input), nameBuf))
        nameToFind = tokenName;

    // 3. The roster, by name.
    auto **roster = Game::Read<uint8_t **>(Offsets::VAR_GUILD_ROSTER_PTR);
    const int total = Game::Read<int>(Offsets::VAR_GUILD_ROSTER_TOTAL_COUNT);
    if (roster == nullptr || total <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    auto cmp = reinterpret_cast<SStrCmpI_t>(Offsets::FUN_SSTR_CMP_I);
    for (int i = 0; i < total; ++i) {
        const uint8_t *entry = roster[i];
        if (entry == nullptr)
            continue;
        const char *name =
            Game::Ptr<const char>(entry, Offsets::OFF_GUILD_MEMBER_NAME);
        if (cmp(name, nameToFind, 0x7FFFFFFF) == 0) {
            Game::Lua::PushNumber(L, 1.0);
            return 1;
        }
    }
    Game::Lua::PushNil(L);
    return 1;
}

// `UnitIsPossessed(unit)` — returns true if the unit is currently
// possessed (priest `Mind Control`, warlock `Subjugate Demon`). Reads
// `UNIT_FIELD_FLAGS` bit 24 (`UNIT_FLAG_POSSESSED = 0x01000000`)
// directly off the unit's m_objectFields descriptor. Same broadcast
// path as FEIGN_DEATH — works for any unit token.
int __fastcall Script_UnitIsPossessed(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitIsPossessed(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }

    auto *unit = static_cast<const uint8_t *>(Game::ResolveUnitToken(token));
    if (unit == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    auto *fields =
        Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const uint32_t unitFlags =
        Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);
    Game::Lua::PushBoolean(L,
        (unitFlags & Offsets::UNIT_FLAG_POSSESSED) != 0);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitIsAFK", &Script_UnitIsAFK);
    Game::Lua::RegisterGlobalFunction("UnitIsDND", &Script_UnitIsDND);
    Game::Lua::RegisterGlobalFunction("UnitIsFeignDeath", &Script_UnitIsFeignDeath);
    Game::Lua::RegisterGlobalFunction("UnitIsPossessed", &Script_UnitIsPossessed);
    Game::Lua::RegisterGlobalFunction("UnitIsInMyGuild", &Script_UnitIsInMyGuild);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Flags
