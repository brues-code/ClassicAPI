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

#include "Game.h"
#include "Offsets.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>

namespace Map::Info {

namespace {

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);
using GroupMemberStatsLookup_t = const uint8_t *(__fastcall *)(const uint64_t *guid);

// Parse a positive 1-based decimal integer from `s`. Returns -1 if `s`
// is empty, has non-digit chars, or doesn't fit `[1, max]`. Used to
// turn the trailing digits of "party3" / "raid17" into a 0-based slot.
int ParseSlot(const char *s, int max) {
    if (s == nullptr || *s == '\0')
        return -1;
    int value = 0;
    for (const char *p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9')
            return -1;
        value = value * 10 + (*p - '0');
        if (value > max)
            return -1;
    }
    return value >= 1 ? value - 1 : -1;
}

// Look up the GUID stored in the party member slot directly. Slots are
// 0..3 in the parallel GUID table at `VAR_PARTY_MEMBER_GUIDS` (8-byte
// stride). Returns 0 if the slot is empty (no party member there).
uint64_t PartySlotGUID(int slot) {
    if (slot < 0 || slot >= 4)
        return 0;
    auto *guids = reinterpret_cast<const uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PARTY_MEMBER_GUIDS));
    return guids[slot];
}

// Raid slot storage at `0x00B712A8` is an array of 40 `RaidMember *`
// pointers; the GUID sits at `*member + 0` (lo) and `*member + 4`
// (hi). Verified from `FUN_004bb0f0`'s disassembly:
//   MOV EAX, [ECX*4 + 0xb712a8]   ; entry = arr[i]
//   MOV EDI, [EAX]                 ; guid_lo
//   MOV EAX, [EAX + 4]             ; guid_hi
uint64_t RaidSlotGUID(int slot) {
    if (slot < 0 || slot >= 40)
        return 0;
    auto *const *arr = reinterpret_cast<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_RAID_MEMBER_PTRS));
    const uint8_t *entry = arr[slot];
    if (entry == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(entry);
}

uint16_t LocalPlayerArea() {
    return static_cast<uint16_t>(*reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_AREA_ID)));
}

uint64_t LocalPlayerGUID() { return Unit::Identity::PlayerGuid(); }


// Pushes `area` as a number, or nil if `area == 0`. Vanilla zone 0
// shows up before the engine has initialized the player's location;
// we treat it as "unknown" rather than returning a spurious zero.
void PushAreaOrNil(void *L, uint16_t area) {
    if (area == 0) {
        Game::Lua::PushNil(L);
    } else {
        Game::Lua::PushNumber(L, static_cast<double>(area));
    }
}

} // namespace

// `C_Map.GetBestMapForUnit(unitToken)` — returns the AreaTable.dbc area
// ID for the given unit. Vanilla 1.12 has no UiMap.db2 concept; the
// closest equivalent is the zone-level AreaTable ID that the engine
// itself tracks for the local player and broadcasts for party/raid
// members via SMSG_PARTY_MEMBER_STATS_FULL.
//
// Coverage:
// - `"player"` — direct read of `VAR_PLAYER_AREA_ID`.
// - `"target"`, `"mouseover"`, `"focus"`, etc. that resolve to the
//   local player — same.
// - `"party1".."party4"`, `"raid1".."raid40"`, and tokens resolving to
//   any party/raid member's GUID — read via the unified
//   `FUN_GROUP_MEMBER_STATS_LOOKUP` engine helper.
// - Anything else (NPCs, units outside the player's group) — nil.
//
// **The returned ID is a vanilla AreaTable.dbc ID, not a modern UI map
// ID.** Addons that hardcode modern IDs (e.g. `if uiMapID == 84 then`
// for retail Stormwind) will not match. Use IDs produced by this same
// function or by `GetCurrentMapAreaID`-style helpers from this backport.
static int __fastcall Script_GetBestMapForUnit(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    // Fast path: "player" doesn't need token resolution.
    if (std::strcmp(token, "player") == 0) {
        PushAreaOrNil(L, LocalPlayerArea());
        return 1;
    }

    // "partyN" / "raidN" — parse directly so we work for members in
    // other zones (no local CGUnit, so the engine's token resolver
    // would return NULL). Slot-indexed GUID tables are populated
    // continuously by party/raid sync regardless of sync range.
    uint64_t guid = 0;
    if (std::strncmp(token, "party", 5) == 0) {
        guid = PartySlotGUID(ParseSlot(token + 5, 4));
    } else if (std::strncmp(token, "raid", 4) == 0) {
        guid = RaidSlotGUID(ParseSlot(token + 4, 40));
    } else {
        // Use TokenToGUID directly so we get the GUID for tokens like
        // "target" / "mouseover" even when the unit has no local CGUnit
        // (e.g., name-based /target of a party member in another zone).
        auto fn = reinterpret_cast<TokenToGUID_t>(
            static_cast<uintptr_t>(Offsets::FUN_TOKEN_TO_GUID));
        guid = fn(token);
    }
    if (guid == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }

    // Token resolved to the local player (e.g. "target" on yourself).
    if (guid == LocalPlayerGUID()) {
        PushAreaOrNil(L, LocalPlayerArea());
        return 1;
    }

    auto lookup = reinterpret_cast<GroupMemberStatsLookup_t>(
        static_cast<uintptr_t>(Offsets::FUN_GROUP_MEMBER_STATS_LOOKUP));
    const uint8_t *stats = lookup(&guid);
    if (stats == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const uint16_t area = *reinterpret_cast<const uint16_t *>(
        stats + Offsets::OFF_GROUP_MEMBER_AREA_ID);
    PushAreaOrNil(L, area);
    return 1;
}

const Game::Doc::Field kBestMapForUnitArgs[] = {
    Game::Doc::Req("unitToken", "UnitToken"),
};
const Game::Doc::Field kBestMapForUnitRets[] = {
    Game::Doc::Opt("uiMapID", "number", nullptr,
                   "The area ID of the unit's zone; nil for a unit that is not the player or "
                   "in the player's group."),
};
const Game::Doc::Function kGetBestMapForUnit{
    "Returns the zone a unit is in.", kBestMapForUnitArgs, kBestMapForUnitRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "GetBestMapForUnit",
                                     &Script_GetBestMapForUnit, &kGetBestMapForUnit);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Map::Info
