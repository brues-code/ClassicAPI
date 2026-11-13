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

// `C_Map` user waypoint — the single player-placed map pin:
//
//   CanSetUserWaypointOnMap(uiMapID)   -> bool
//   SetUserWaypoint(uiMapPoint)        -> wasSet
//   GetUserWaypoint()                  -> UiMapPoint
//   HasUserWaypoint()                  -> bool
//   ClearUserWaypoint()
//   GetUserWaypointPositionForMap(id)  -> vector2
//   GetUserWaypointHyperlink()         -> worldmap link string
//   GetUserWaypointFromHyperlink(link) -> UiMapPoint
//
// Fires `USER_WAYPOINT_UPDATED` whenever the waypoint changes.
//
// The waypoint is one piece of client-side state: a `uiMapID` (zone or
// continent — see `Map::MapInfo`), a 0..1 position on that map, and an
// optional `z`. It lives for the session and survives a UI reload.
//
// `GetUserWaypointPositionForMap` re-projects the pin onto a DIFFERENT map by
// going through world coordinates (`Map::Area`'s row transforms), so a pin
// dropped on a zone still resolves on the continent map and the reverse.
//
// The hyperlink is the standard `worldmap:uiMapID:x:y` payload with both
// coordinates scaled by 10000, wrapped in a clickable link.

#include "Game.h"
#include "event/Custom.h"
#include "map/Area.h"
#include "map/Vector2.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Map::Waypoint {

namespace {

const Game::Doc::Event kUpdatedDoc{"Map", "Fires when the user waypoint is set or cleared.", {}};
const Event::Custom::AutoReserve _evtUpdated{"USER_WAYPOINT_UPDATED", &kUpdatedDoc};

struct UserWaypoint {
    bool has = false;
    int uiMapID = 0;
    double x = 0.0;
    double y = 0.0;
    bool hasZ = false;
    double z = 0.0;
};

UserWaypoint g_wp;

void FireUpdated() { Event::Custom::Fire(_evtUpdated.Slot(), ""); }

// A map can hold a pin when it has a usable coordinate rect. The world map's
// row carries an all-zero rect, so it correctly reports false.
bool MapSupportsWaypoint(int uiMapID) {
    const int row = Map::Area::RowForUiMapID(uiMapID);
    double left, right, top, bottom;
    if (row <= 0 || !Map::Area::RowRect(row, &left, &right, &top, &bottom, nullptr))
        return false;
    return (left - right) > 0.0 && (top - bottom) > 0.0;
}

// Pushes a UiMapPoint table: { uiMapID, position (vector2), z? }.
void PushUiMapPoint(void *L, int uiMapID, double x, double y, bool hasZ, double z) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "uiMapID", static_cast<double>(uiMapID));
    Game::Lua::PushString(L, "position");
    Map::PushVector2D(L, x, y);
    Game::Lua::SetTable(L, -3);
    if (hasZ)
        Game::Lua::SetFieldNumber(L, "z", z);
}

// Reads a UiMapPoint from the table at absolute stack index `idx`.
bool ReadUiMapPoint(void *L, int idx, int *uiMapID, double *x, double *y,
                    bool *hasZ, double *z) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_TABLE)
        return false;

    Game::Lua::PushString(L, "uiMapID");
    Game::Lua::GetTable(L, idx);
    const bool idOk = Game::Lua::IsNumber(L, -1) != 0;
    if (idOk)
        *uiMapID = static_cast<int>(Game::Lua::ToNumber(L, -1));
    Game::Lua::SetTop(L, -2);
    if (!idOk)
        return false;

    Game::Lua::PushString(L, "position");
    Game::Lua::GetTable(L, idx);
    const int posIdx = Game::Lua::GetTop(L);
    const bool posOk = Map::ReadVector2D(L, posIdx, x, y);
    Game::Lua::SetTop(L, -2);
    if (!posOk)
        return false;

    Game::Lua::PushString(L, "z");
    Game::Lua::GetTable(L, idx);
    *hasZ = Game::Lua::IsNumber(L, -1) != 0;
    if (*hasZ)
        *z = Game::Lua::ToNumber(L, -1);
    Game::Lua::SetTop(L, -2);
    return true;
}

// Parses `worldmap:<uiMapID>:<x>:<y>` out of `s` — either the bare payload or
// a full `|Hworldmap:…|h[…]|h` link. Coordinates arrive scaled by 10000.
bool ParseWorldmapLink(const char *s, int *uiMapID, double *x, double *y) {
    if (s == nullptr)
        return false;
    const char *p = std::strstr(s, "worldmap:");
    if (p == nullptr)
        return false;
    p += 9; // past "worldmap:"

    char *end = nullptr;
    const long id = std::strtol(p, &end, 10);
    if (end == p || *end != ':')
        return false;
    p = end + 1;
    const long xi = std::strtol(p, &end, 10);
    if (end == p || *end != ':')
        return false;
    p = end + 1;
    const long yi = std::strtol(p, &end, 10);
    if (end == p)
        return false;

    *uiMapID = static_cast<int>(id);
    *x = static_cast<double>(xi) / 10000.0;
    *y = static_cast<double>(yi) / 10000.0;
    return true;
}

// `C_Map.CanSetUserWaypointOnMap(uiMapID)` — whether a pin can be placed on
// the given map.
int __fastcall Script_CanSetUserWaypointOnMap(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1) != 0;
    const int uiMapID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const bool can = ok && MapSupportsWaypoint(uiMapID);
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBool(L, can);
    return 1;
}

// `C_Map.SetUserWaypoint(uiMapPoint)` — places the pin. Returns whether it
// was set (false for a malformed point or a map that can't hold one).
int __fastcall Script_SetUserWaypoint(void *L) {
    int uiMapID = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    bool hasZ = false;
    const bool ok = ReadUiMapPoint(L, 1, &uiMapID, &x, &y, &hasZ, &z) &&
                    MapSupportsWaypoint(uiMapID);

    Game::Lua::SetTop(L, 0);
    if (!ok) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    g_wp.has = true;
    g_wp.uiMapID = uiMapID;
    g_wp.x = x;
    g_wp.y = y;
    g_wp.hasZ = hasZ;
    g_wp.z = z;
    FireUpdated();
    Game::Lua::PushBool(L, true);
    return 1;
}

// `C_Map.GetUserWaypoint()` — the pin as a UiMapPoint, or nil.
int __fastcall Script_GetUserWaypoint(void *L) {
    Game::Lua::SetTop(L, 0);
    if (!g_wp.has) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushUiMapPoint(L, g_wp.uiMapID, g_wp.x, g_wp.y, g_wp.hasZ, g_wp.z);
    return 1;
}

// `C_Map.HasUserWaypoint()` — whether a pin is placed.
int __fastcall Script_HasUserWaypoint(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushBool(L, g_wp.has);
    return 1;
}

// `C_Map.ClearUserWaypoint()` — removes the pin.
int __fastcall Script_ClearUserWaypoint(void *L) {
    const bool had = g_wp.has;
    g_wp = UserWaypoint{};
    Game::Lua::SetTop(L, 0);
    if (had)
        FireUpdated();
    return 0;
}

// `C_Map.GetUserWaypointPositionForMap(uiMapID)` — the pin's position on the
// given map as a vector2, or nil when it doesn't land on that map. Re-projects
// through world coordinates, so a zone pin resolves on the continent too.
int __fastcall Script_GetUserWaypointPositionForMap(void *L) {
    const bool ok = Game::Lua::IsNumber(L, 1) != 0;
    const int wantID = ok ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;

    bool have = false;
    double px = 0.0, py = 0.0;
    if (ok && g_wp.has) {
        if (wantID == g_wp.uiMapID) {
            px = g_wp.x;
            py = g_wp.y;
            have = true;
        } else {
            const int fromRow = Map::Area::RowForUiMapID(g_wp.uiMapID);
            const int toRow = Map::Area::RowForUiMapID(wantID);
            double worldX = 0.0, worldY = 0.0;
            have = fromRow > 0 && toRow > 0 &&
                   Map::Area::WorldFromRow(fromRow, g_wp.x, g_wp.y, &worldX, &worldY) &&
                   Map::Area::PercentInRow(toRow, static_cast<float>(worldX),
                                           static_cast<float>(worldY), &px, &py) &&
                   px >= 0.0 && px <= 1.0 && py >= 0.0 && py <= 1.0;
        }
    }

    Game::Lua::SetTop(L, 0);
    if (!have) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Map::PushVector2D(L, px, py);
    return 1;
}

// `C_Map.GetUserWaypointHyperlink()` — a clickable worldmap link for the pin,
// or nil when none is placed.
int __fastcall Script_GetUserWaypointHyperlink(void *L) {
    const bool has = g_wp.has;
    const int uiMapID = g_wp.uiMapID;
    const long xi = static_cast<long>(g_wp.x * 10000.0 + 0.5);
    const long yi = static_cast<long>(g_wp.y * 10000.0 + 0.5);

    Game::Lua::SetTop(L, 0);
    if (!has) {
        Game::Lua::PushNil(L);
        return 1;
    }
    char link[160];
    std::snprintf(link, sizeof(link),
                  "|cffffff00|Hworldmap:%d:%ld:%ld|h[Map Pin Location]|h|r",
                  uiMapID, xi, yi);
    Game::Lua::PushString(L, link);
    return 1;
}

// `C_Map.GetUserWaypointFromHyperlink(hyperlink)` — the UiMapPoint a worldmap
// link describes, or nil when the string isn't one.
int __fastcall Script_GetUserWaypointFromHyperlink(void *L) {
    const char *s = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    int uiMapID = 0;
    double x = 0.0, y = 0.0;
    const bool ok = ParseWorldmapLink(s, &uiMapID, &x, &y);

    Game::Lua::SetTop(L, 0);
    if (!ok) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushUiMapPoint(L, uiMapID, x, y, /*hasZ=*/false, 0.0);
    return 1;
}

const Game::Doc::Field kUiMapPointFields[] = {
    Game::Doc::Req("uiMapID", "number"),
    Game::Doc::Req("position", "vector2", "The point on the map, from 0 to 1 on each axis."),
    Game::Doc::Opt("z", "number", nullptr, "Nil when the point has no height."),
};
const Game::Doc::Structure kUiMapPoint{"UiMapPoint", "Map", kUiMapPointFields,
                                       "A point on a map."};

const Game::Doc::Field kUiMapIDArgs[] = {
    Game::Doc::Req("uiMapID", "number"),
};

const Game::Doc::Field kCanSetRets[] = {
    Game::Doc::Req("canSet", "bool", "False for a map with no world coordinates."),
};
const Game::Doc::Function kCanSetUserWaypointOnMap{
    "Returns whether the user waypoint can be placed on a map.", kUiMapIDArgs, kCanSetRets};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("point", "UiMapPoint"),
};
const Game::Doc::Field kSetRets[] = {
    Game::Doc::Req("wasSet", "bool",
                   "False for a malformed point or a map that cannot hold the waypoint."),
};
const Game::Doc::Function kSetUserWaypoint{"Places the user waypoint, the player's map pin.",
                                           kSetArgs, kSetRets};

const Game::Doc::Field kGetRets[] = {
    Game::Doc::Opt("point", "UiMapPoint", nullptr, "Nil when no waypoint is set."),
};
const Game::Doc::Function kGetUserWaypoint{"Returns the user waypoint.", {}, kGetRets};

const Game::Doc::Field kHasRets[] = {
    Game::Doc::Req("hasUserWaypoint", "bool"),
};
const Game::Doc::Function kHasUserWaypoint{"Returns whether the user waypoint is set.", {},
                                           kHasRets};

const Game::Doc::Function kClearUserWaypoint{"Removes the user waypoint.", {}, {}};

const Game::Doc::Field kPositionForMapRets[] = {
    Game::Doc::Opt("mapPosition", "vector2", nullptr,
                   "From 0 to 1 on each axis; nil when no waypoint is set or the waypoint is "
                   "not on the map."),
};
const Game::Doc::Function kGetUserWaypointPositionForMap{
    "Returns the position of the user waypoint on a map.", kUiMapIDArgs, kPositionForMapRets};

const Game::Doc::Field kHyperlinkRets[] = {
    Game::Doc::Opt("hyperlink", "string", nullptr, "Nil when no waypoint is set."),
};
const Game::Doc::Function kGetUserWaypointHyperlink{
    "Returns a chat link to the user waypoint.", {}, kHyperlinkRets};

const Game::Doc::Field kFromHyperlinkArgs[] = {
    Game::Doc::Req("hyperlink", "string", "A whole link, or text that contains its worldmap part."),
};
const Game::Doc::Field kFromHyperlinkRets[] = {
    Game::Doc::Opt("point", "UiMapPoint", nullptr,
                   "Nil when the text holds no map pin link; the point has no z."),
};
const Game::Doc::Function kGetUserWaypointFromHyperlink{
    "Returns the map point that a map pin link describes.", kFromHyperlinkArgs,
    kFromHyperlinkRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "CanSetUserWaypointOnMap",
                                     &Script_CanSetUserWaypointOnMap,
                                     &kCanSetUserWaypointOnMap);
    Game::Lua::RegisterTableFunction("C_Map", "SetUserWaypoint",
                                     &Script_SetUserWaypoint, &kSetUserWaypoint);
    Game::Lua::RegisterTableFunction("C_Map", "GetUserWaypoint",
                                     &Script_GetUserWaypoint, &kGetUserWaypoint);
    Game::Lua::RegisterTableFunction("C_Map", "HasUserWaypoint",
                                     &Script_HasUserWaypoint, &kHasUserWaypoint);
    Game::Lua::RegisterTableFunction("C_Map", "ClearUserWaypoint",
                                     &Script_ClearUserWaypoint, &kClearUserWaypoint);
    Game::Lua::RegisterTableFunction("C_Map", "GetUserWaypointPositionForMap",
                                     &Script_GetUserWaypointPositionForMap,
                                     &kGetUserWaypointPositionForMap);
    Game::Lua::RegisterTableFunction("C_Map", "GetUserWaypointHyperlink",
                                     &Script_GetUserWaypointHyperlink,
                                     &kGetUserWaypointHyperlink);
    Game::Lua::RegisterTableFunction("C_Map", "GetUserWaypointFromHyperlink",
                                     &Script_GetUserWaypointFromHyperlink,
                                     &kGetUserWaypointFromHyperlink);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Map::Waypoint
