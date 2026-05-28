// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "nameplate/Walk.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Unit::Identity {

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);

// FUN_TOKEN_TO_GUID's `"player"` path resolves the GUID by looking the
// local player up in the engine's object manager: it calls FUN_00468550
// to read `[VAR_LOCAL_PLAYER_PTR + 0xC0]`, then `FUN_00468460(type=0x10,
// guid)` against the object manager — and returns 0 if that lookup
// misses, even though `+0xC0` already holds the right GUID. The
// object-manager entry can drop transiently (zone transitions, brief
// CGPlayer rebuilds) while `+0xC0` stays valid, so for the bare
// `"player"` token we read the canonical pointer directly. Modern
// WoW's `UnitGUID("player")` doesn't gate on object-manager state
// either. Suffixed tokens (`"playertarget"` and the like) still go
// through the engine so the chained `.target` walk works.
//
// This does NOT help at addon file-load time on first login. The
// engine populates `+0xC0` from SMSG_UPDATE_OBJECT processing in the
// main loop, which happens AFTER `FrameScript_Initialize` (and the
// addon load pass it triggers) finishes. The engine itself gates its
// own post-init player setup at the tail of FrameScript_Initialize on
// `FUN_00468550() != 0` for the same reason — the GUID isn't ready
// yet. Addons that need the player GUID at boot must wait for
// `PLAYER_LOGIN`. See the trace notes in commit history.
static uint64_t ResolveTokenGUID(const char *token) {
    if (token != nullptr && _stricmp(token, "player") == 0) {
        auto *player = *reinterpret_cast<uint8_t *const *>(
            static_cast<uintptr_t>(Offsets::VAR_LOCAL_PLAYER_PTR));
        if (player == nullptr)
            return 0;
        return *reinterpret_cast<const uint64_t *>(
            player + Offsets::OFF_LOCAL_PLAYER_GUID);
    }
    auto fn = reinterpret_cast<TokenToGUID_t>(Offsets::FUN_TOKEN_TO_GUID);
    return fn(token);
}

// `UnitGUID(unit)` — returns the unit's 64-bit GUID formatted as a
// hex string `"0xHHHHHHHHLLLLLLLL"` (16 hex digits, hi dword first).
//
// 1.12 GUIDs are plain 64-bit integers — no type-prefix system like
// modern WoW's `"Player-1234-..."` / `"Creature-0-..."` shapes. Vanilla
// addons that tracked units across events used this `"0x..."` format,
// and 3.3.5's `Script_UnitGUID` (at `0x0060E630` in the Frostmourne
// client) emits the same shape after a much longer code path that
// special-cases sentinel GUID values introduced post-2.x.
//
// Returns `nil` if:
//   - the unit token resolves to a GUID of zero (e.g. `"target"`
//     when nothing's targeted; `"partyN"` when the slot is empty).
//
// Works for out-of-range party / raid members. The engine maintains
// a parallel GUID array populated by `SMSG_GROUP_LIST` that's
// independent of whether the unit's CGObject is currently active
// in the client. We call `FUN_TOKEN_TO_GUID` directly rather than
// going through `FUN_RESOLVE_UNIT_TOKEN` — the latter does an extra
// `FUN_00468460` step that returns NULL when the CGObject isn't
// live, which is why earlier versions of `UnitGUID` returned nil
// for `"party1"` when the member was on a different continent.
//
// Invalid tokens (arbitrary strings that aren't unit IDs) raise a
// Lua error via the engine's `"Unknown unit name: %s"` path — same
// as `Script_UnitName` and other engine unit-token consumers.
static int __fastcall Script_UnitGUID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitGUID(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr)
        return 0;

    const uint64_t guid = ResolveTokenGUID(token);
    if (guid == 0)
        return 0;

    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(guid, buf, sizeof buf));
    return 1;
}

// `UnitTokenFromGUID(guid)` — best-effort reverse lookup: given a unit
// GUID, return the first unit token currently mapped to that GUID, or
// nil if none of the known tokens point at it.
//
// The search walks the modern retail token order with post-vanilla
// tokens dropped (no `vehicle`, `arenaN`, `arenapetN`, `bossN`,
// `softenemy`, `softfriend`, `softinteract` — those all post-date
// 1.12 and `FUN_TOKEN_TO_GUID` would raise "Unknown unit name" for
// them). `nameplateN` and `focus` are included — we hook the token
// resolver so the engine recognizes them (see
// `unit/TokenExtensions.cpp` and `unit/Focus.cpp`). Order matches
// retail with the dropped tokens skipped in place:
//
//   player → pet → party1..4 → partypet1..4 → raid1..40
//          → raidpet1..40 → nameplate1..N → target → focus
//          → npc → mouseover
//
// The result is inherently unstable — multiple tokens can map to the
// same GUID at once (your target IS your mouseover IS your party1), and
// the mapping changes across frames as `SMSG_GROUP_LIST` /
// `SMSG_AURA_UPDATE` etc. fire. Same caveat as modern's API; addons
// that cache the return should re-verify with `UnitGUID(token)`
// before each use.
//
// Returns `nil` if the input string isn't a parseable GUID, or the
// GUID is zero, or no token currently maps to it.
static int __fastcall Script_UnitTokenFromGUID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitTokenFromGUID(\"unitGUID\")");
        return 0;
    }
    const char *guidStr = Game::Lua::ToString(L, 1);
    uint64_t target = 0;
    if (guidStr == nullptr || !Guid::Parse(guidStr, &target) || target == 0)
        return 0;

    auto check = [&](const char *token) -> bool {
        if (ResolveTokenGUID(token) != target)
            return false;
        Game::Lua::PushString(L, token);
        return true;
    };

    if (check("player")) return 1;
    if (check("pet")) return 1;

    // Cap each indexed-token scan at the actual populated slot
    // count, read straight out of engine memory. Solo skips all 88
    // group tokens; a 5-person party scans 8 instead of 88. Raid
    // and party are NOT mutually exclusive — in a 40-man raid the
    // engine still tracks the player's 5-person subgroup in the
    // party array, so we iterate both independently when both have
    // populated slots.
    //
    // Party count = number of non-zero GUIDs in the 4-slot party
    // GUID array. Same walk `Script_GetNumPartyMembers` does
    // internally (`FUN_004E86D0`), inlined here.
    const auto *partyGuids = reinterpret_cast<const uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PARTY_GUIDS));
    int partyCount = 0;
    for (int i = 0; i < Offsets::PARTY_MAX_SLOTS; ++i) {
        if (partyGuids[i] != 0)
            ++partyCount;
    }
    int raidCount = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_RAID_MEMBER_COUNT));
    if (raidCount > Offsets::RAID_MAX_SLOTS)
        raidCount = Offsets::RAID_MAX_SLOTS;

    char buf[16];
    for (int i = 1; i <= partyCount; ++i) {
        std::snprintf(buf, sizeof buf, "party%d", i);
        if (check(buf)) return 1;
    }
    for (int i = 1; i <= partyCount; ++i) {
        std::snprintf(buf, sizeof buf, "partypet%d", i);
        if (check(buf)) return 1;
    }
    for (int i = 1; i <= raidCount; ++i) {
        std::snprintf(buf, sizeof buf, "raid%d", i);
        if (check(buf)) return 1;
    }
    for (int i = 1; i <= raidCount; ++i) {
        std::snprintf(buf, sizeof buf, "raidpet%d", i);
        if (check(buf)) return 1;
    }

    // Visible nameplates. The ordered list (see
    // `NamePlate::Events::g_orderedGUIDs`) is dense — `GetGUIDByIndex`
    // returns 0 once we step past the populated count, terminating
    // the loop. Skips the token-resolver hook + SStrCmpI cost per
    // index by comparing GUIDs directly.
    for (int i = 1; ; ++i) {
        const uint64_t plateGuid = NamePlate::Events::GetGUIDByIndex(i);
        if (plateGuid == 0)
            break;
        if (plateGuid == target) {
            std::snprintf(buf, sizeof buf, "nameplate%d", i);
            Game::Lua::PushString(L, buf);
            return 1;
        }
    }

    if (check("target")) return 1;
    if (check("focus")) return 1;
    if (check("npc")) return 1;
    if (check("mouseover")) return 1;

    return 0;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitGUID", &Script_UnitGUID);
    Game::Lua::RegisterGlobalFunction("UnitTokenFromGUID", &Script_UnitTokenFromGUID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Identity
