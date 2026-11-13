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

// `C_Map` coordinate transforms — the world↔map projection layer that sits on
// top of `WorldMapArea.dbc`'s per-zone placement rects:
//
//   GetPlayerMapPosition(uiMapID, unitToken) -> position(vector2)
//   GetWorldPosFromMapPos(uiMapID, mapPos)   -> continentID, worldPos(vector2)
//   GetMapPosFromWorldPos(continentID, worldPos [, overrideUiMapID])
//                                            -> uiMapID, mapPos(vector2)
//   GetMapRectOnMap(uiMapID, topUiMapID)     -> minX, maxX, minY, maxY
//
// ## The identity scheme (shared with the rest of `Map::*`)
//
// `uiMapID` is a vanilla `AreaTable.dbc` area id for a zone — the same
// identity `C_Map.GetBestMapForUnit` returns and `GetMapWorldSize` /
// `GetMapOverlays` accept — or a NEGATIVE `-(WorldMapArea row)` for a
// continent / world / instance map (see `Map::Area`'s uiMapID section), so a
// continent map works anywhere a zone does. `continentID` is a `Map.dbc` map
// id (0 = Eastern Kingdoms, 1 = Kalimdor, or an instance map). World
// coordinates are continent-space
// (WoW world axes: +X north, +Y west), exactly what `AreaTrigger.dbc` stores
// and what the object `GetPosition` virtual returns. All four functions reuse
// `Map::Area`'s zone rect math (`PercentInZone`, `ZonePercent`,
// `ContinentPercent`, `RowForAreaID`) so the world↔map convention lives in one
// place.
//
// ## vector2 returns
//
// Positions are returned as `Vector2DMixin` objects (built through the
// `CreateVector2D` global) so consumers can call `pos:GetXY()` / read `pos.x`.
// If that global is unavailable, a plain `{x=, y=}` table carries the same
// coordinates.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "map/Area.h"
#include "map/Vector2.h"
#include "unit/Position.h"

#include <cstdint>

namespace Map::Coordinates {

namespace {

// True when a projected 0..1 map position actually lands on the map.
bool OnMap(double px, double py) {
    return px >= 0.0 && px <= 1.0 && py >= 0.0 && py <= 1.0;
}

using Map::PushVector2D;

// Reads `t.x` / `t.y` from the table at absolute stack index `idx`.
bool ReadXY(void *L, int idx, double *x, double *y) {
    return Map::ReadVector2D(L, idx, x, y);
}

// `C_Map.GetPlayerMapPosition(uiMapID, unitToken)` — the unit's normalized
// (0..1) position within zone `uiMapID`, or nil when the unit's live world
// position isn't inside that zone's rect (wrong zone, an instance with no map
// row, or no position yet). Invalid unit tokens error like other unit
// functions (`UnitHealth("garbage")`).
int __fastcall Script_GetPlayerMapPosition(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1) && Game::Lua::IsString(L, 2);
    const int areaID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const char *token = ok ? Game::Lua::ToString(L, 2) : nullptr;

    const int row = ok ? Map::Area::RowForUiMapID(areaID) : -1;
    float pos[3];
    double px = 0.0, py = 0.0;
    const bool have = ok && token != nullptr && row > 0 &&
                      Unit::Position::ReadToken(token, pos) &&
                      Map::Area::PercentInRow(row, pos[0], pos[1], &px, &py) &&
                      OnMap(px, py);

    Game::Lua::SetTop(L, 0);
    if (!have) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushVector2D(L, px, py);
    return 1;
}

// `C_Map.GetWorldPosFromMapPos(uiMapID, mapPosition)` — the continent-space
// world position of a normalized (0..1) point on zone `uiMapID`. Returns
// `continentID, worldPosition`; nil when the zone has no map row. Inverse of
// `GetMapPosFromWorldPos`.
int __fastcall Script_GetWorldPosFromMapPos(void *L) {
    const bool ok =
        Game::Lua::IsNumber(L, 1) && Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE;
    const int areaID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;

    const int row = ok ? Map::Area::RowForUiMapID(areaID) : -1;
    double mx = 0.0, my = 0.0;
    double worldX = 0.0, worldY = 0.0;
    int continentID = 0;
    const bool have = ok && ReadXY(L, 2, &mx, &my) && row > 0 &&
                      Map::Area::RowRect(row, nullptr, nullptr, nullptr, nullptr,
                                         &continentID) &&
                      Map::Area::WorldFromRow(row, mx, my, &worldX, &worldY);

    Game::Lua::SetTop(L, 0);
    if (!have) {
        Game::Lua::PushNil(L);
        return 1;
    }

    Game::Lua::PushNumber(L, static_cast<double>(continentID));
    PushVector2D(L, worldX, worldY); // vector.x = world X (north), .y = world Y (west)
    return 2;
}

// `C_Map.GetMapPosFromWorldPos(continentID, worldPosition [, overrideUiMapID])`
// — the map position of a continent-space world point. With `overrideUiMapID`,
// projects into that specific zone (nil if the point is outside its rect);
// without it, resolves the zone the point falls in. Returns `uiMapID,
// mapPosition`. (Vanilla has no continent-level uiMapID, so the no-override
// form resolves to the containing zone rather than continent-normalizing.)
int __fastcall Script_GetMapPosFromWorldPos(void *L) {
    const bool ok =
        Game::Lua::IsNumber(L, 1) && Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE;
    const int continentID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const bool hasOverride = Game::Lua::IsNumber(L, 3);
    const int overrideArea = hasOverride ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : 0;

    double wx = 0.0, wy = 0.0;
    const bool readOk = ok && ReadXY(L, 2, &wx, &wy);

    int areaID = 0;
    double mapX = 0.0, mapY = 0.0;
    bool have = false;
    if (readOk) {
        if (hasOverride) {
            const int row = Map::Area::RowForUiMapID(overrideArea);
            double px = 0.0, py = 0.0;
            have = row > 0 &&
                   Map::Area::PercentInRow(row, static_cast<float>(wx),
                                           static_cast<float>(wy), &px, &py) &&
                   OnMap(px, py);
            if (have) {
                areaID = overrideArea;
                mapX = px * 100.0; // normalized below with the ZonePercent path
                mapY = py * 100.0;
            }
        } else {
            have = Map::Area::ZonePercent(continentID, static_cast<float>(wx),
                                          static_cast<float>(wy), &areaID, &mapX, &mapY);
        }
    }

    Game::Lua::SetTop(L, 0);
    if (!have) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(areaID));
    PushVector2D(L, mapX / 100.0, mapY / 100.0);
    return 2;
}

// `C_Map.GetMapRectOnMap(uiMapID, topUiMapID)` — the normalized (0..1)
// rectangle that zone `uiMapID` occupies on its continent map, as
// `minX, maxX, minY, maxY`. `topUiMapID` names the containing continent; since
// vanilla has no continent uiMapID, the rect is computed on the zone's own
// continent. Returns nothing (→ nil ×4) when the zone has no map row.
int __fastcall Script_GetMapRectOnMap(void *L) {
    const int areaID = Game::Lua::IsNumber(L, 1) ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const int topID = Game::Lua::IsNumber(L, 2) ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 0;

    const int row = Map::Area::RowForUiMapID(areaID);
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    int continentID = 0;
    double aPx = 0.0, aPy = 0.0, bPx = 0.0, bPy = 0.0;
    bool have = row > 0 && Map::Area::RowRect(row, &left, &right, &top, &bottom,
                                              &continentID);
    if (have) {
        // `topUiMapID` names the map to measure against; fall back to the
        // zone's own continent when it isn't a usable map.
        int topRow = (topID != 0) ? Map::Area::RowForUiMapID(topID) : -1;
        if (topRow <= 0)
            topRow = Map::Area::ContinentRowForMapID(continentID);
        // Project the rect's two extreme world corners onto that map.
        // Corner A = (world X=top, Y=left); B = (bottom, right).
        have = topRow > 0 &&
               Map::Area::PercentInRow(topRow, static_cast<float>(top),
                                       static_cast<float>(left), &aPx, &aPy) &&
               Map::Area::PercentInRow(topRow, static_cast<float>(bottom),
                                       static_cast<float>(right), &bPx, &bPy);
    }

    Game::Lua::SetTop(L, 0);
    if (!have)
        return 0; // nil, nil, nil, nil

    const double minX = aPx < bPx ? aPx : bPx;
    const double maxX = aPx > bPx ? aPx : bPx;
    const double minY = aPy < bPy ? aPy : bPy;
    const double maxY = aPy > bPy ? aPy : bPy;
    Game::Lua::PushNumber(L, minX);
    Game::Lua::PushNumber(L, maxX);
    Game::Lua::PushNumber(L, minY);
    Game::Lua::PushNumber(L, maxY);
    return 4;
}

const Game::Doc::Field kPlayerMapPositionArgs[] = {
    Game::Doc::Req("uiMapID", "number"),
    Game::Doc::Req("unitToken", "UnitToken"),
};
const Game::Doc::Field kPlayerMapPositionRets[] = {
    Game::Doc::Opt("position", "vector2", nullptr,
                   "From 0 to 1 on each axis; nil when the unit is not on the map."),
};
const Game::Doc::Function kGetPlayerMapPosition{
    "Returns a unit's position on a map.", kPlayerMapPositionArgs, kPlayerMapPositionRets};

const Game::Doc::Field kWorldPosFromMapPosArgs[] = {
    Game::Doc::Req("uiMapID", "number"),
    Game::Doc::Req("mapPosition", "vector2", "The point on the map, from 0 to 1 on each axis."),
};
const Game::Doc::Field kWorldPosFromMapPosRets[] = {
    Game::Doc::Opt("continentID", "number", nullptr,
                   "The continent or instance map; nil, with worldPosition, when the map has no "
                   "world coordinates."),
    Game::Doc::Opt("worldPosition", "vector2", nullptr, "x runs north and y runs west."),
};
const Game::Doc::Function kGetWorldPosFromMapPos{
    "Converts a point on a map to world coordinates.", kWorldPosFromMapPosArgs,
    kWorldPosFromMapPosRets};

const Game::Doc::Field kMapPosFromWorldPosArgs[] = {
    Game::Doc::Req("continentID", "number", "The continent or instance map."),
    Game::Doc::Req("worldPosition", "vector2", "x runs north and y runs west."),
    Game::Doc::Opt("overrideUiMapID", "number", nullptr,
                   "Puts the point on this map instead of on the zone it is in."),
};
const Game::Doc::Field kMapPosFromWorldPosRets[] = {
    Game::Doc::Opt("uiMapID", "number", nullptr,
                   "The zone the point is in, or overrideUiMapID; nil, with mapPosition, when "
                   "no map holds the point."),
    Game::Doc::Opt("mapPosition", "vector2", nullptr, "From 0 to 1 on each axis."),
};
const Game::Doc::Function kGetMapPosFromWorldPos{
    "Converts world coordinates to a point on a map.", kMapPosFromWorldPosArgs,
    kMapPosFromWorldPosRets};

const Game::Doc::Field kMapRectOnMapArgs[] = {
    Game::Doc::Req("uiMapID", "number"),
    Game::Doc::Req("topUiMapID", "number",
                   "The map to measure on; 0 or an ID that names no map uses the zone's own "
                   "continent."),
};
const Game::Doc::Field kMapRectOnMapRets[] = {
    Game::Doc::Opt("minX", "number", nullptr,
                   "Nil, with the rest, when either map has no world coordinates."),
    Game::Doc::Opt("maxX", "number"),
    Game::Doc::Opt("minY", "number"),
    Game::Doc::Opt("maxY", "number"),
};
const Game::Doc::Function kGetMapRectOnMap{
    "Returns the rectangle a map covers on another map, from 0 to 1 on each axis.",
    kMapRectOnMapArgs, kMapRectOnMapRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "GetPlayerMapPosition",
                                     &Script_GetPlayerMapPosition, &kGetPlayerMapPosition);
    Game::Lua::RegisterTableFunction("C_Map", "GetWorldPosFromMapPos",
                                     &Script_GetWorldPosFromMapPos, &kGetWorldPosFromMapPos);
    Game::Lua::RegisterTableFunction("C_Map", "GetMapPosFromWorldPos",
                                     &Script_GetMapPosFromWorldPos, &kGetMapPosFromWorldPos);
    Game::Lua::RegisterTableFunction("C_Map", "GetMapRectOnMap",
                                     &Script_GetMapRectOnMap, &kGetMapRectOnMap);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Map::Coordinates
