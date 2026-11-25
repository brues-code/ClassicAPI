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

// `markN` raid-target-marker unit tokens (`mark1`=star … `mark8`=skull).
//
// Two halves, mirroring `Unit::Focus`:
//
//   1. Resolution — `GetGUIDByMark` reads the engine's marker table
//      (`VAR_RAID_TARGET_GUIDS`); the `markN` branch of the token
//      resolver in `unit/TokenExtensions.cpp` calls it so every stock
//      `Script_Unit*` (UnitHealth, UnitExists, UnitName, …) accepts
//      `"markN"` for free.
//
//   2. Events — every unit event the engine fires for a marked unit also
//      fires with `arg1 == "markN"` (`unit/TokenEvents.cpp`, which reads
//      `GetGUIDByMark`). The server writes the marker table
//      (`SMSG_RAID_TARGET_UPDATE`) with no event of its own, so a WorldTick
//      diff of the 8 slots tells TokenEvents which units gained or lost a
//      mark. A marked unit coming into range needs nothing here: the engine
//      evaluates it as it arrives. The idle case (no markers set) costs 8
//      compares.
//
// A marker on a non-unit GUID (totem corpse / gameobject / loot) never
// resolves to a `CGUnit`, so it fires no events — correct, and matches
// `UnitExists("markN")` being false there.

#include "unit/RaidTarget.h"

#include "Game.h"
#include "Offsets.h"
#include "lua/Arg.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"
#include "unit/Relation.h"
#include "unit/TokenEvents.h"

#include <cstdint>

namespace Unit::RaidTarget {

namespace {

// The marker table as of the last tick, to see which slots changed.
uint64_t g_markGUID[kMarkCount] = {};

const uint64_t *MarkerTable() {
    return reinterpret_cast<const uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_RAID_TARGET_GUIDS));
}

void OnWorldTick() {
    const uint64_t *table = MarkerTable();
    for (int i = 0; i < kMarkCount; ++i) {
        const uint64_t now = table[i];
        if (now == g_markGUID[i])
            continue;
        const uint64_t old = g_markGUID[i];
        g_markGUID[i] = now;
        TokenEvents::Refresh(old);
        TokenEvents::Refresh(now);
    }
}

const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// Reset the mirror on logout→character-select: the DLL isn't reloaded across
// a relog, and every unit object went with the world. Glue hook so it fires
// on logout but NOT on /reload.
void ClearOnLogout() {
    for (int i = 0; i < kMarkCount; ++i)
        g_markGUID[i] = 0;
}
const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

// A marker a new holder can take: nobody wears it, or (when asked) its
// holder is a dead unit that is not friendly. A holder that is not loaded
// counts as alive.
bool Available(int mark, bool deadNonFriendly) {
    const uint64_t guid = MarkerTable()[mark - 1];
    if (guid == 0)
        return true;
    if (!deadNonFriendly)
        return false;
    const void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid);
    const uint8_t *fields = unit != nullptr ? Object::Fields(unit) : nullptr;
    const void *player = Unit::Identity::PlayerObject();
    return fields != nullptr && player != nullptr &&
           Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) <= 0 &&
           Unit::Relation::Reaction(unit, player) <= 3;
}

// GetNextAvailableRaidTargetMarkerIndex(startIndex [, reverseSearch,
// wrapSearch, treatDeadNonFriendlyAsAvailable]) — retail only (no 3.80.2
// code): from startIndex up (or down) through 1..8, once round with
// wrapSearch, the first marker available; 0 when none is.
int __fastcall Script_GetNextAvailableRaidTargetMarkerIndex(void *L) {
    constexpr const char *kUsage =
        "Usage: local nextAvailableRaidTargetMarkerIndex = GetNextAvailableRaidTargetMarkerIndex(startIndex [, "
        "reverseSearch, wrapSearch, treatDeadNonFriendlyAsAvailable])";
    uint32_t start;
    bool reverse = false, wrap = false, deadNonFriendly = false;
    if (!Game::Lua::Arg::Index(L, 1, kUsage, start) || !Game::Lua::Arg::Bool(L, 2, kUsage, reverse, true) ||
        !Game::Lua::Arg::Bool(L, 3, kUsage, wrap, true) || !Game::Lua::Arg::Bool(L, 4, kUsage, deadNonFriendly, true))
        return 0;
    const int step = reverse ? -1 : 1;
    int slot = static_cast<int32_t>(start);
    int found = 0;
    for (int k = 0; k < kMarkCount; ++k, slot += step) {
        if (wrap)
            slot = (slot % kMarkCount + kMarkCount) % kMarkCount;
        else if (slot < 0 || slot >= kMarkCount)
            break;
        if (Available(slot + 1, deadNonFriendly)) {
            found = slot + 1;
            break;
        }
    }
    Game::Lua::PushNumber(L, found);
    return 1;
}

const Game::Doc::Field kNextArgs[] = {
    Game::Doc::Req("startIndex", "luaIndex"),
    Game::Doc::Opt("reverseSearch", "bool", "false"),
    Game::Doc::Opt("wrapSearch", "bool", "false"),
    Game::Doc::Opt("treatDeadNonFriendlyAsAvailable", "bool", "false"),
};
const Game::Doc::Field kNextRets[] = {
    Game::Doc::Req("nextAvailableRaidTargetMarkerIndex", "luaIndex", "0 when no marker is available."),
};
const Game::Doc::Function kGetNextAvailableRaidTargetMarkerIndex{
    "Returns the first raid target marker from startIndex that nobody wears.", kNextArgs, kNextRets, "RaidMarker"};

// Retail's RaidMarkerConstantsDocumentation, values as generated.
const Game::Lua::EnumIntegerEntry kRaidMarkerConsts[] = {
    {"MAX_RAID_TARGETS_USER", kMarkCount},
    {"MAX_RAID_TARGETS_RESTRICTED", kMarkCount},
    {"MAX_VALID_RAID_TARGETS", 0},
    {"MAX_RAID_MARKERS", kMarkCount},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetNextAvailableRaidTargetMarkerIndex",
                                      &Script_GetNextAvailableRaidTargetMarkerIndex,
                                      &kGetNextAvailableRaidTargetMarkerIndex);
    Game::Lua::RegisterConstants("RaidMarkerConsts", kRaidMarkerConsts, "RaidMarker");
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

uint64_t GetGUIDByMark(int mark) {
    if (mark < 1 || mark > kMarkCount)
        return 0;
    return MarkerTable()[mark - 1];
}

int MarkOf(uint64_t guid) {
    using FindSlot_t = int(__stdcall *)(uint32_t guidLo, uint32_t guidHi);
    if (guid == 0)
        return 0;
    const int slot = reinterpret_cast<FindSlot_t>(Offsets::FUN_RAID_TARGET_FIND_SLOT)(
        static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
    return slot < kMarkCount ? slot + 1 : 0;
}

} // namespace Unit::RaidTarget
