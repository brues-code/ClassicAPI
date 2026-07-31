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

#include "unit/Identity.h"

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "nameplate/Walk.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Unit::Identity {

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);

// The selected character's GUID, captured at the glue "Enter World" click
// (the FUN_GLUE_ENTER_WORLD co-hook below) — i.e. before the engine has
// created the in-world player object whose +0xC0 holds the live GUID.
// `PlayerGuid()` serves this as a fallback during the early-login window so
// `UnitGUID("player")` answers from addon-load time instead of returning nil
// until SMSG_UPDATE_OBJECT lands (≈PLAYER_LOGIN). 0 = not captured yet.
// Overwritten on every enter, so a character switch can't serve a stale GUID;
// only ever read when the live engine value is still 0, and identical to it
// once that's populated.
static uint64_t g_charSelectGuid = 0;

// `FUN_GLUE_ENTER_WORLD` is `void(void)` (no args). Co-hook it to snapshot the
// selected character's GUID once the entry is committed.
using EnterWorld_t = void(__cdecl *)();
static EnterWorld_t g_origEnterWorld = nullptr;

static void __cdecl EnterWorld_h() {
    g_origEnterWorld();
    // Only on a genuine commit — the rename / char-locked bail paths return
    // before the worker sets the state code to "entering world".
    if (*reinterpret_cast<const int *>(
            static_cast<uintptr_t>(Offsets::VAR_GLUE_LOGIN_INPROGRESS)) !=
        Offsets::GLUE_STATE_ENTERING_WORLD)
        return;
    const uintptr_t charStruct = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_GLUE_SELECTED_CHAR));
    if (charStruct != 0)
        g_charSelectGuid = *reinterpret_cast<const uint64_t *>(
            charStruct + Offsets::OFF_GLUE_CHAR_GUID);
}

static const Game::HookAutoRegister _enterWorldHook{
    Offsets::FUN_GLUE_ENTER_WORLD, reinterpret_cast<void *>(&EnterWorld_h),
    reinterpret_cast<void **>(&g_origEnterWorld)};

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
// The engine populates `+0xC0` from SMSG_UPDATE_OBJECT processing in the
// main loop, which happens AFTER `FrameScript_Initialize` (and the addon
// load pass it triggers) finishes — so the engine value is 0 at addon
// file-load on first login. We bridge that window with `g_charSelectGuid`,
// captured at the glue "Enter World" click (see the co-hook above): the
// selected character's GUID is known well before the player object exists,
// and it's the same value `+0xC0` will hold once it's live. So
// `UnitGUID("player")` / `PlayerGuid()` answer correctly from addon-load
// time. (The player *object* still doesn't exist until `+0xC0` populates —
// this only makes the GUID *value* available early, for keying SavedVariables,
// comparisons, our aura/cast caster checks, etc.)
uint64_t PlayerGuid() {
    auto *player = *reinterpret_cast<uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCAL_PLAYER_PTR));
    if (player != nullptr) {
        const uint64_t guid = *reinterpret_cast<const uint64_t *>(
            player + Offsets::OFF_LOCAL_PLAYER_GUID);
        if (guid != 0)
            return guid; // live engine value — authoritative
    }
    return g_charSelectGuid; // early-login fallback (0 until char-select)
}

const uint8_t *PlayerObject() {
    // Resolve the player's CGObject straight from the object manager by GUID —
    // one hash lookup, no token-string parsing, and non-throwing: the resolver
    // returns null when the GUID isn't loaded (pre-world) instead of raising,
    // so this is safe from any context (world tick, packet send hook, Lua
    // callback). The GUID comes from PlayerGuid() — 0 before char-select, which
    // short-circuits to null here. NOT VAR_LOCAL_PLAYER_PTR: its +0xC0 is only
    // the GUID, not the canonical CGPlayer whose descriptor is readable.
    const uint64_t guid = PlayerGuid();
    if (guid == 0)
        return nullptr;
    using ResolveByGuid_t = void *(__fastcall *)(uint32_t typeMask,
                                                 const char *debugName,
                                                 uint32_t guidLo, uint32_t guidHi,
                                                 int line);
    auto resolve =
        reinterpret_cast<ResolveByGuid_t>(Offsets::FUN_OBJECT_RESOLVE_BY_GUID);
    return static_cast<const uint8_t *>(
        resolve(Offsets::OBJ_TYPE_PLAYER, "ClassicAPI",
                static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32),
                /*line*/ 0));
}

const uint8_t *PlayerInventoryManager() {
    const uint8_t *player = PlayerObject();
    if (player == nullptr)
        return nullptr;
    return player + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
}

const uint8_t *PlayerDescriptor() {
    const uint8_t *player = PlayerObject();
    if (player == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_UNIT_DESCRIPTOR);
}

uint64_t GuidForObject(const void *unitObject) {
    if (unitObject == nullptr)
        return 0;
    const auto *unit = static_cast<const uint8_t *>(unitObject);
    const uint8_t *block = *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_UNIT_GUID_PTR);
    if (block == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(block);
}

bool IsPlayerToken(const char *token) {
    if (token == nullptr)
        return false;
    // Mirror Script_UnitClass/Script_UnitRace byte-for-byte: they compare the
    // token via Storm's SStrCmpI(token, "player", 0x7FFFFFFF) (the call at
    // 0x005183B0), not the CRT _stricmp. Same case-insensitive result for the
    // ASCII literal, but routed through the engine's own comparator.
    using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);
    auto sstrcmpi = reinterpret_cast<SStrCmpI_t>(Offsets::FUN_SSTR_CMP_I);
    return sstrcmpi(token, "player", 0x7FFFFFFF) == 0;
}

uint64_t GuidForToken(const char *token) {
    if (IsPlayerToken(token))
        return PlayerGuid();
    auto fn = reinterpret_cast<TokenToGUID_t>(Offsets::FUN_TOKEN_TO_GUID);
    return fn(token);
}

const uint8_t *PlayerInfoRecord(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    // Peek the engine's player-info NameCache: a NULL callback means "look up
    // only, don't fire a query" (see the FUN_PLAYER_INFO_LOOKUP note in
    // Offsets.h). Returns the entry data block for a cached GUID, else null.
    using Lookup_t = const uint8_t *(__thiscall *)(void *cache, uint32_t lo,
                                                   uint32_t hi, void *cookie,
                                                   void *cb, void *ud, char flag);
    auto fn = reinterpret_cast<Lookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_PLAYER_INFO_LOOKUP));
    uint32_t cookie[2] = {0, 0};
    return fn(reinterpret_cast<void *>(
                  static_cast<uintptr_t>(Offsets::VAR_PLAYER_NAME_CACHE)),
              static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32),
              cookie, nullptr, nullptr, 0);
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

    const uint64_t guid = GuidForToken(token);
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
const char *TokenFromGUID(uint64_t target, char *buf, size_t bufSize) {
    if (target == 0)
        return nullptr;

    auto set = [&](const char *token) -> const char * {
        std::snprintf(buf, bufSize, "%s", token);
        return buf;
    };
    auto check = [&](const char *token) -> bool {
        return GuidForToken(token) == target;
    };

    if (check("player")) return set("player");
    if (check("pet")) return set("pet");

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

    for (int i = 1; i <= partyCount; ++i) {
        std::snprintf(buf, bufSize, "party%d", i);
        if (GuidForToken(buf) == target) return buf;
    }
    for (int i = 1; i <= partyCount; ++i) {
        std::snprintf(buf, bufSize, "partypet%d", i);
        if (GuidForToken(buf) == target) return buf;
    }
    for (int i = 1; i <= raidCount; ++i) {
        std::snprintf(buf, bufSize, "raid%d", i);
        if (GuidForToken(buf) == target) return buf;
    }
    for (int i = 1; i <= raidCount; ++i) {
        std::snprintf(buf, bufSize, "raidpet%d", i);
        if (GuidForToken(buf) == target) return buf;
    }

    // Visible nameplates. The slot array is SPARSE (retail-exact slot
    // assignment — freed middle slots stay as gaps until reused), so we
    // scan `1..GetSlotCount()` and skip empties rather than stopping at
    // the first 0. Compares GUIDs directly, skipping the token-resolver
    // hook + SStrCmpI cost per index.
    const int plateCount = NamePlate::Events::GetSlotCount();
    for (int i = 1; i <= plateCount; ++i) {
        if (NamePlate::Events::GetGUIDByIndex(i) == target) {
            std::snprintf(buf, bufSize, "nameplate%d", i);
            return buf;
        }
    }

    if (check("target")) return set("target");
    if (check("focus")) return set("focus");
    if (check("npc")) return set("npc");
    if (check("mouseover")) return set("mouseover");

    return nullptr;
}

int TokensForGUID(uint64_t guid, char (*out)[32], int maxTokens) {
    if (guid == 0 || out == nullptr || maxTokens <= 0)
        return 0;

    int n = 0;
    // Engine-native tokens via the reverse map. It fills a shared static
    // array and returns its base; snapshot each string into `out` before we
    // do anything re-entrant. Non-throwing (count 0 pre-world).
    using TokensFromGuid_t =
        const char *const *(__fastcall *)(const uint64_t *guid, int *outCount);
    int nativeCount = 0;
    const char *const *natives = reinterpret_cast<TokensFromGuid_t>(
        static_cast<uintptr_t>(Offsets::FUN_UNIT_TOKENS_FROM_GUID))(&guid,
                                                                    &nativeCount);
    if (natives != nullptr) {
        for (int i = 0; i < nativeCount && n < maxTokens; ++i) {
            const char *tok = natives[i];
            if (tok == nullptr)
                continue;
            // SuperWoW patches this reverse map to ALSO emit the caster's raw
            // GUID string ("0x…") as a token (it makes GUID strings valid unit
            // tokens). Skip it — the standard tokens are what retail-compat
            // addons expect; echoing the GUID we were handed is noise.
            if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
                continue;
            std::snprintf(out[n], 32, "%s", tok);
            ++n;
        }
    }

    // Synthetic tokens the engine's map can't produce. `focus` resolves via
    // our token-resolver hook (returns 0 when unset — never raises).
    if (n < maxTokens && GuidForToken("focus") == guid)
        std::snprintf(out[n++], 32, "focus");

    // Visible nameplates — sparse slot array, so scan and skip gaps.
    const int plateCount = NamePlate::Events::GetSlotCount();
    for (int i = 1; i <= plateCount && n < maxTokens; ++i) {
        if (NamePlate::Events::GetGUIDByIndex(i) == guid)
            std::snprintf(out[n++], 32, "nameplate%d", i);
    }

    return n;
}

static int __fastcall Script_UnitTokenFromGUID(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitTokenFromGUID(\"unitGUID\")");
        return 0;
    }
    const char *guidStr = Game::Lua::ToString(L, 1);
    uint64_t target = 0;
    if (guidStr == nullptr || !Guid::Parse(guidStr, &target) || target == 0)
        return 0;

    char buf[32];
    const char *token = TokenFromGUID(target, buf, sizeof buf);
    if (token == nullptr)
        return 0;
    Game::Lua::PushString(L, token);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitGUID", &Script_UnitGUID);
    Game::Lua::RegisterGlobalFunction("UnitTokenFromGUID", &Script_UnitTokenFromGUID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Identity
