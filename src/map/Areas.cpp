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

// `C_Map.GetAreaInfo(areaID)` and `C_Map.GetAreas()` — surface
// `AreaTable.dbc`'s localized name column to Lua. `AreaTable.dbc` is a
// client DBC (always loaded, synchronous, localized, ~1500 rows), so
// unlike creature/item names there's no WDB cold-cache problem — the name
// is always available for any id.
//
// - `GetAreaInfo(areaID)` is a retail backport: the localized name for one
//   area id (zone or subzone), matching retail's signature.
// - `GetAreas()` is a ClassicAPI extension (retail uses the uiMapID tree,
//   not raw AreaTable enumeration): the full id→name map, the load-bearing
//   piece for name databases that need name→id / the full id set (reverse
//   search, name resolution). Same "surface the hidden DBC" pattern as
//   `GetAreaTriggers` / `GetMapOverlays`.
//
// Names are the client's active locale (same source as `GetRealZoneText` /
// `Script_GetCharacterInfo`'s zone-name resolve), so they match what the
// game shows everywhere — no locale/rename drift vs a shipped table.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "dbc/Names.h"

#include <cstdint>

namespace Map::Areas {

namespace {

// `C_Map.GetAreaInfo(areaID)` — localized AreaTable name, or nil for
// non-numeric / non-positive input or an id with no row / empty name.
int __fastcall Script_GetAreaInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int areaID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *name =
        (areaID > 0) ? DBC::AreaName(static_cast<uint32_t>(areaID),
                                     /*resolveToParent=*/false)
                     : nullptr;
    if (name == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, name);
    return 1;
}

// `C_Map.GetAreas()` — { [areaID] = name } for every row with a non-empty
// localized name. Empty-name rows (DBC::LocalizedField → nullptr) are
// omitted, so the map round-trips with GetAreaInfo for every key.
int __fastcall Script_GetAreas(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const int count = Game::Read<int>(Offsets::VAR_AREATABLE_COUNT);
    for (int id = 1; id <= count; ++id) {
        const char *name =
            DBC::AreaName(static_cast<uint32_t>(id), /*resolveToParent=*/false);
        if (name == nullptr)
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(id));
        Game::Lua::PushString(L, name);
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// `C_Map.GetMapAreaIDs()` — { [mapName] = areaID } for every
// `WorldMapArea.dbc` row: the map's name (the dir the engine uses for its map
// textures) → its `AreaTable` areaID. Vanilla has no `GetCurrentMapAreaID`;
// this lets an addon resolve a *browsed* map to a zone id — including the
// city / instance maps `GetMapZones()` doesn't enumerate (Orgrimmar,
// Undercity, Alterac Valley), which otherwise can't be matched to zone content
// (e.g. a flight master pinned by areaID). Continent-spanning rows (areaID 0)
// and unnamed rows are omitted.
//
// Note: this is the WorldMapArea NAME, which matches `GetMapInfo()`'s returned
// dir for almost all maps but not a few misspelled Blizzard folders (the
// Orgrimmar map's texture folder is "Ogrimmar"), so `[GetMapInfo()]` can miss
// for those — resolve them by zone name instead.
int __fastcall Script_GetMapAreaIDs(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_AREA_COUNT);
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                         Offsets::VAR_WORLDMAP_AREA_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        const int areaID = Game::Read<int>(rec, Offsets::OFF_WMA_AREA_ID);
        if (areaID == 0)
            continue; // continent-spanning row, no zone id
        const char *dir = Game::Read<const char *>(rec, Offsets::OFF_WMA_NAME);
        if (dir == nullptr || dir[0] == '\0')
            continue;
        Game::Lua::SetFieldNumber(L, dir, static_cast<double>(areaID));
    }
    return 1;
}

const Game::Doc::Field kAreaInfoArgs[] = {
    Game::Doc::Req("areaID", "number"),
};
const Game::Doc::Field kAreaInfoRets[] = {
    Game::Doc::Opt("name", "cstring", nullptr, "Nil when no area has the ID."),
};
const Game::Doc::Function kGetAreaInfo{
    "Returns the localized name of a zone or subzone.", kAreaInfoArgs, kAreaInfoRets};

const Game::Doc::Field kAreasRets[] = {
    Game::Doc::Req("areas", "table", "Keyed by area ID; each value is the area's localized name."),
};
const Game::Doc::Function kGetAreas{
    "Returns the name of every zone and subzone, keyed by area ID.", {}, kAreasRets, nullptr,
    true};

const Game::Doc::Field kMapAreaIDsRets[] = {
    Game::Doc::Req("areaIDs", "table",
                   "Keyed by the map's texture folder name; each value is the zone's area ID."),
};
const Game::Doc::Function kGetMapAreaIDs{
    "Returns the area ID of every zone map, keyed by the map's texture folder name.", {},
    kMapAreaIDsRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "GetAreaInfo", &Script_GetAreaInfo,
                                     &kGetAreaInfo);
    Game::Lua::RegisterTableFunction("C_Map", "GetAreas", &Script_GetAreas, &kGetAreas);
    Game::Lua::RegisterTableFunction("C_Map", "GetMapAreaIDs", &Script_GetMapAreaIDs,
                                     &kGetMapAreaIDs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Map::Areas
