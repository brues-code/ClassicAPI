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

// `C_Map` map hierarchy — the tree the coordinate layer hangs off:
//
//   GetMapInfo(uiMapID)               -> UiMapDetails table
//   GetMapChildrenInfo(uiMapID)       -> array of UiMapDetails
//   GetMapInfoAtPosition(uiMapID,x,y) -> UiMapDetails at a point
//   GetFallbackWorldMapID()           -> the world map's uiMapID
//
// ## The tree
//
// Three levels, all read from `WorldMapArea.dbc` + `Map.dbc`:
//
//   World (the "World" row)
//     +- Continent (Kalimdor, Eastern Kingdoms)
//          +- Zone (Durotar, Elwynn Forest, ...)
//
// A zone is named by its positive `AreaTable.dbc` area id. The World and
// continent levels carry no AreaTable id (their `WorldMapArea` rows have
// areaID 0), so they use the negative `-(WorldMapArea row)` half of the
// uiMapID space — see the uiMapID section in `Map::Area`. Instance maps
// (dungeons, raids, battlegrounds) are the same shape as continents: an
// areaID-0 row, addressed negatively.
//
// A zone's parent is the continent-spanning row of the `Map.dbc` map it sits
// on. Each continent's parent is the World row. The World row's own rect is
// all zeroes, so it names the map but carries no geometry — the coordinate
// functions correctly report nothing for it.
//
// ## mapType
//
// `Enum.UIMapType`: Cosmic 0, World 1, Continent 2, Zone 3, Dungeon 4. A
// positive id is always a Zone. A negative id takes its type from the
// `Map.dbc` instance type of the map it names (0 = world map → Continent,
// anything else → Dungeon), except the World row itself.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "dbc/Names.h"
#include "map/Area.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Map::MapInfo {

namespace {

// Enum.UIMapType values we can produce.
constexpr int kMapTypeWorld = 1;
constexpr int kMapTypeContinent = 2;
constexpr int kMapTypeZone = 3;
constexpr int kMapTypeDungeon = 4;

const uint8_t *MapRecord(int mapID) {
    return DBC::Record(Offsets::VAR_MAP_RECORDS, Offsets::VAR_MAP_COUNT, static_cast<uint32_t>(mapID));
}

// Localized `Map.dbc` display name ("Eastern Kingdoms", "Deadmines"), or null.
const char *MapName(int mapID) {
    const uint8_t *rec = MapRecord(mapID);
    if (rec == nullptr)
        return nullptr;
    const uint32_t locale = Game::Read<uint32_t>(Offsets::VAR_LOCALE_INDEX);
    const char *name = *reinterpret_cast<const char *const *>(
        rec + Offsets::OFF_MAP_NAME + locale * 4);
    if (name == nullptr || name[0] == '\0')
        return nullptr;
    return name;
}

// `Map.dbc` instance type (0 = world map, 1 = dungeon, 2 = raid, 3 = BG).
// -1 when the map has no row.
int MapInstanceType(int mapID) {
    const uint8_t *rec = MapRecord(mapID);
    if (rec == nullptr)
        return -1;
    return Game::Read<int>(rec, Offsets::OFF_MAP_INSTANCE_TYPE);
}

// The WorldMapArea directory name of a row ("Kalimdor", "World", ""), or null.
const char *RowName(int row) {
    if (row <= 0)
        return nullptr;
    const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                     Offsets::VAR_WORLDMAP_AREA_COUNT,
                                     static_cast<uint32_t>(row));
    if (rec == nullptr)
        return nullptr;
    return Game::Read<const char *>(rec, Offsets::OFF_WMA_NAME);
}

bool IsWorldRow(int row) {
    const char *name = RowName(row);
    return name != nullptr && std::strcmp(name, "World") == 0;
}

// The WorldMapArea row the client uses for the whole-world map: the areaID-0
// row named "World". -1 when the client has none.
int WorldRow() {
    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_AREA_COUNT);
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                         Offsets::VAR_WORLDMAP_AREA_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr ||
            Game::Read<int>(rec, Offsets::OFF_WMA_AREA_ID) != 0)
            continue;
        if (IsWorldRow(id))
            return id;
    }
    return -1;
}

// Builds the UiMapDetails table for `uiMapID` and leaves it on the stack top.
// Returns false (pushing nothing) when the id names no map.
bool PushMapInfo(void *L, int uiMapID) {
    const int row = Map::Area::RowForUiMapID(uiMapID);
    if (row <= 0)
        return false;

    const char *name = nullptr;
    int mapType = kMapTypeZone;
    int parentMapID = 0;

    if (uiMapID > 0) {
        // A zone: AreaTable name, parented to its continent.
        name = DBC::AreaName(static_cast<uint32_t>(uiMapID),
                             /*resolveToParent=*/false);
        mapType = kMapTypeZone;
        int continentID = 0;
        if (Map::Area::RowRect(row, nullptr, nullptr, nullptr, nullptr,
                               &continentID)) {
            const int contRow = Map::Area::ContinentRowForMapID(continentID);
            if (contRow > 0)
                parentMapID = -contRow;
        }
    } else if (IsWorldRow(row)) {
        // The whole-world map: named by the row itself, no parent.
        name = RowName(row);
        mapType = kMapTypeWorld;
        parentMapID = 0;
    } else {
        // A continent or instance map: Map.dbc supplies the display name and
        // the type; continents hang off the world map.
        int mapID = 0;
        Map::Area::RowRect(row, nullptr, nullptr, nullptr, nullptr, &mapID);
        name = MapName(mapID);
        if (name == nullptr)
            name = RowName(row); // custom map with no Map.dbc row
        const int instanceType = MapInstanceType(mapID);
        if (instanceType == 0) {
            mapType = kMapTypeContinent;
            const int worldRow = WorldRow();
            if (worldRow > 0)
                parentMapID = -worldRow;
        } else {
            mapType = kMapTypeDungeon;
        }
    }

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "mapID", static_cast<double>(uiMapID));
    Game::Lua::SetFieldString(L, "name", name);
    Game::Lua::SetFieldNumber(L, "mapType", static_cast<double>(mapType));
    Game::Lua::SetFieldNumber(L, "parentMapID", static_cast<double>(parentMapID));
    Game::Lua::SetFieldNumber(L, "flags", 0.0);
    return true;
}

// `C_Map.GetMapInfo(uiMapID)` — the map's details table, or nil.
int __fastcall Script_GetMapInfo(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1);
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    Game::Lua::SetTop(L, 0);
    if (!ok || !PushMapInfo(L, uiMapID))
        Game::Lua::PushNil(L);
    return 1;
}

// `C_Map.GetMapChildrenInfo(uiMapID)` — the maps one level below `uiMapID`:
// the continents of the world map, or the zones of a continent / instance map.
// Always returns a table (empty for a zone, which has no children).
int __fastcall Script_GetMapChildrenInfo(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1);
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const int row = ok ? Map::Area::RowForUiMapID(uiMapID) : -1;

    // A zone (positive id) has no children; anything else enumerates below.
    const bool wantContinents = (row > 0) && IsWorldRow(row);
    int parentMapDbcID = 0;
    const bool wantZones = (row > 0) && uiMapID < 0 && !wantContinents &&
                           Map::Area::RowRect(row, nullptr, nullptr, nullptr,
                                              nullptr, &parentMapDbcID);

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (!wantContinents && !wantZones)
        return 1;

    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_AREA_COUNT);
    int outIdx = 0;
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                         Offsets::VAR_WORLDMAP_AREA_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        const int areaID = Game::Read<int>(rec, Offsets::OFF_WMA_AREA_ID);
        const int mapID = Game::Read<int>(rec, Offsets::OFF_WMA_MAP_ID);

        int childID = 0;
        if (wantContinents) {
            // Continent-spanning rows of real world maps, minus the World row.
            if (areaID != 0 || id == row || MapInstanceType(mapID) != 0)
                continue;
            if (Map::Area::ContinentRowForMapID(mapID) != id)
                continue; // skip the stray zero-rect duplicate
            childID = -id;
        } else {
            // Zone rows of this map.
            if (areaID == 0 || mapID != parentMapDbcID)
                continue;
            childID = areaID;
        }

        outIdx += 1;
        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        if (!PushMapInfo(L, childID)) {
            Game::Lua::SetTop(L, -2); // drop the index; nothing to store
            outIdx -= 1;
            continue;
        }
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// `C_Map.GetMapInfoAtPosition(uiMapID, x, y)` — the zone at a normalized
// (0..1) point on `uiMapID`, or nil when the point is on no zone.
int __fastcall Script_GetMapInfoAtPosition(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1) && Game::Lua::IsNumber(L, 2) &&
                    Game::Lua::IsNumber(L, 3);
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const double px = ok ? Game::Lua::ToNumber(L, 2) : 0.0;
    const double py = ok ? Game::Lua::ToNumber(L, 3) : 0.0;

    const int row = ok ? Map::Area::RowForUiMapID(uiMapID) : -1;
    double worldX = 0.0, worldY = 0.0;
    int mapID = 0;
    int areaID = 0;
    double mapX = 0.0, mapY = 0.0;
    const bool have =
        row > 0 &&
        Map::Area::RowRect(row, nullptr, nullptr, nullptr, nullptr, &mapID) &&
        Map::Area::WorldFromRow(row, px, py, &worldX, &worldY) &&
        Map::Area::ZonePercent(mapID, static_cast<float>(worldX),
                               static_cast<float>(worldY), &areaID, &mapX, &mapY);

    Game::Lua::SetTop(L, 0);
    if (!have || !PushMapInfo(L, areaID))
        Game::Lua::PushNil(L);
    return 1;
}

// A map's background is this many tiles (`NUM_WORLDMAP_DETAIL_TILES`).
constexpr int kWorldMapDetailTiles = 12;

bool FileExists(const char *path) {
    using FileExists_t = int(__stdcall *)(const char *path, int mode);
    auto fn = reinterpret_cast<FileExists_t>(
        static_cast<uintptr_t>(Offsets::FUN_FILE_EXISTS));
    return fn(path, 1) != 0;
}

// Builds tile `n`'s texture path (1-based) for map directory `dir`, without
// the file extension — the form `SetTexture` takes.
void BuildTilePath(char *out, size_t size, const char *dir, int n) {
    std::snprintf(out, size, "Interface\\WorldMap\\%s\\%s%d", dir, dir, n);
}

// True when the client ships the map directory's first background tile —
// the test for "this map has art".
bool DirHasArt(const char *dir) {
    if (dir == nullptr || dir[0] == '\0')
        return false;
    char path[0x120];
    BuildTilePath(path, sizeof(path), dir, 1);
    char probe[0x140];
    std::snprintf(probe, sizeof(probe), "%s.blp", path);
    return FileExists(probe);
}

// `C_Map.MapHasArt(uiMapID)` — whether the map has a drawable background.
//
// A map's art is the tile set `Interface\WorldMap\<dir>\<dir>1..12`, where
// `<dir>` is the WorldMapArea row's own name — the same directory the world
// map frame builds its tile paths from. So the question is answered by
// probing for the first tile: a row with no name, or one whose art the
// client doesn't ship, has none.
//
// Note this is independent of whether a map has a coordinate rect. The
// whole-world map has art but no rect, and an instance row can have a rect
// but ship no art.
int __fastcall Script_MapHasArt(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1);
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const int row = ok ? Map::Area::RowForUiMapID(uiMapID) : -1;

    const bool hasArt = DirHasArt(RowName(row));

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBool(L, hasArt);
    return 1;
}

// `C_Map.GetMapArtLayers(uiMapID)` — the map's art layers, as an array of
// layer descriptions. Always returns a table; a map with no art gives an
// empty one.
//
// This client draws a map's background as one fixed layer, so there is at
// most a single entry, and it is the same for every map that has art: a
// 1002x668 canvas tiled in 256-pixel squares (a 4x3 grid — the twelve tiles
// the world map frame draws). There is no zoom within a map, so the scale
// range is 1 to 1 with no extra zoom steps.
int __fastcall Script_GetMapArtLayers(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1);
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const int row = ok ? Map::Area::RowForUiMapID(uiMapID) : -1;
    const bool hasArt = DirHasArt(RowName(row));

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (!hasArt)
        return 1;

    Game::Lua::PushNumber(L, 1.0);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "layerWidth", Map::Area::kMapCanvasWidth);
    Game::Lua::SetFieldNumber(L, "layerHeight", Map::Area::kMapCanvasHeight);
    Game::Lua::SetFieldNumber(L, "tileWidth",
                              static_cast<double>(Map::Area::kMapTileSize));
    Game::Lua::SetFieldNumber(L, "tileHeight",
                              static_cast<double>(Map::Area::kMapTileSize));
    Game::Lua::SetFieldNumber(L, "minScale", 1.0);
    Game::Lua::SetFieldNumber(L, "maxScale", 1.0);
    Game::Lua::SetFieldNumber(L, "additionalZoomSteps", 0.0);
    Game::Lua::SetTable(L, -3);
    return 1;
}

// `C_Map.GetMapArtLayerTextures(uiMapID, layerIndex)` — the background tile
// textures for one of a map's art layers, in draw order (left to right, top
// to bottom).
//
// A map here has exactly one art layer, the 12-tile background the world map
// frame draws, so `layerIndex` 1 returns it and any other index returns an
// empty table. Always returns a table.
//
// Entries are texture PATHS, ready to hand to `SetTexture`, rather than the
// numeric file ids the modern call returns — this client identifies a texture
// by its path, which is the same substitution `C_Map.GetMapOverlays` makes
// for its `fileDataIDs`. Tiles the client doesn't ship are skipped, so a map
// with no art gives an empty table.
int __fastcall Script_GetMapArtLayerTextures(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1) && Game::Lua::IsNumber(L, 2);
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const int layerIndex = ok ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 0;
    const int row = ok ? Map::Area::RowForUiMapID(uiMapID) : -1;
    const char *dir = (layerIndex == 1) ? RowName(row) : nullptr;

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (dir == nullptr || dir[0] == '\0')
        return 1;

    int outIdx = 0;
    for (int n = 1; n <= kWorldMapDetailTiles; ++n) {
        char path[0x120];
        BuildTilePath(path, sizeof(path), dir, n);
        char probe[0x140];
        std::snprintf(probe, sizeof(probe), "%s.blp", path);
        if (!FileExists(probe))
            continue;
        outIdx += 1;
        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::PushString(L, path);
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// `C_Map.GetFallbackWorldMapID()` — the uiMapID of the whole-world map, the
// safe default when no better map is known.
int __fastcall Script_GetFallbackWorldMapID(void *L) {
    const int row = WorldRow();
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, (row > 0) ? static_cast<double>(-row) : 0.0);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "GetMapInfo", &Script_GetMapInfo);
    Game::Lua::RegisterTableFunction("C_Map", "GetMapChildrenInfo",
                                     &Script_GetMapChildrenInfo);
    Game::Lua::RegisterTableFunction("C_Map", "GetMapInfoAtPosition",
                                     &Script_GetMapInfoAtPosition);
    Game::Lua::RegisterTableFunction("C_Map", "GetFallbackWorldMapID",
                                     &Script_GetFallbackWorldMapID);
    Game::Lua::RegisterTableFunction("C_Map", "MapHasArt", &Script_MapHasArt);
    Game::Lua::RegisterTableFunction("C_Map", "GetMapArtLayerTextures",
                                     &Script_GetMapArtLayerTextures);
    Game::Lua::RegisterTableFunction("C_Map", "GetMapArtLayers",
                                     &Script_GetMapArtLayers);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Map::MapInfo
