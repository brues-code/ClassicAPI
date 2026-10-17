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

#include <cstdint>

// `IsActiveBattlefieldArena()` -> isArena, isRegistered.
//
// The client has no arena battlefield state of its own; what marks an arena is
// its DBC data. An arena match is a pvp-type map (Map.dbc instanceType 3, the
// same type battlegrounds use) whose area carries AreaTable flag 0x80 — the
// flag `Script_GetZonePVPInfo` reports as its isArena return. Verified against
// the extracted DBCs: Turtle's arena maps 26 (Blood Ring) and 28 (Lordaeron
// Arena) are type 3 with areas 4014 / 4013 flagged 0x80, and no battleground
// area has the flag. Server side (tortoise-wow SharedDefines.h,
// GetBattleGrounMapIdByTypeId) the one arena type, BATTLEGROUND_BR, always
// creates map 26; map 28 is client data only. The type-3 gate excludes the
// open-world free-for-all rings (Gurubashi, The Maul, Rumble Cage), which are
// flagged but sit on type-0 continents.
//
// Both the zone and the subzone are checked. `Script_GetZonePVPInfo` reads
// only the subzone, which is 0 on the arena maps: their areas are top-level
// zones with no children, so the engine files them as the zone.
//
// 3.3.5 answers from the battlefield status array (an active arena
// battlefield). Being on an arena map is the same condition for every caller
// that can observe it, and is true from the moment the map loads.
//
// isRegistered (a rated match) is always false: nothing client-side carries a
// rated flag.
namespace PvP::Arena {

namespace {

constexpr uint32_t MAP_INSTANCE_TYPE_PVP = 3;

const uint8_t *DbcRecord(uintptr_t recordsVar, uintptr_t countVar, uint32_t id) {
    const int count = *reinterpret_cast<const int *>(countVar);
    if (id == 0 || static_cast<int>(id) > count)
        return nullptr;
    const uint8_t *const *records =
        *reinterpret_cast<const uint8_t *const *const *>(recordsVar);
    return records != nullptr ? records[id] : nullptr;
}

bool IsArenaArea(uint32_t areaID) {
    const uint8_t *rec = DbcRecord(Offsets::VAR_AREATABLE_RECORDS,
                                   Offsets::VAR_AREATABLE_COUNT, areaID);
    if (rec == nullptr)
        return false;
    const uint32_t flags =
        *reinterpret_cast<const uint32_t *>(rec + Offsets::OFF_AREATABLE_FLAGS);
    return (flags & Offsets::AREA_FLAG_ARENA) != 0;
}

bool InArena() {
    const uint32_t mapID = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_CURRENT_MAP_ID));
    const uint8_t *map =
        DbcRecord(Offsets::VAR_MAP_RECORDS, Offsets::VAR_MAP_COUNT, mapID);
    if (map == nullptr)
        return false;
    const uint32_t type =
        *reinterpret_cast<const uint32_t *>(map + Offsets::OFF_MAP_INSTANCE_TYPE);
    if (type != MAP_INSTANCE_TYPE_PVP)
        return false;

    const uint32_t zone = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_AREA_ID));
    const uint32_t subzone = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SUBZONE_AREA_ID));
    return IsArenaArea(subzone) || IsArenaArea(zone);
}

} // namespace

static int __fastcall Script_IsActiveBattlefieldArena(void *L) {
    Game::Lua::PushBool(L, InArena());
    Game::Lua::PushBool(L, false);
    return 2;
}

static const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isArena", "bool", "True while the player is in an arena match."),
    Game::Doc::Req("isRegistered", "bool", "True for a rated match; always false here."),
};
static const Game::Doc::Function kDoc = {
    "Returns whether the player is in an arena match, and whether it is rated.",
    {}, kRets, "PvPGlobals"};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsActiveBattlefieldArena",
                                      &Script_IsActiveBattlefieldArena, &kDoc);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace PvP::Arena
