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

// `C_Map.GetAreaTriggerInfo(triggerID)` and `C_Map.GetAreaTriggers([mapID])`
// — expose `AreaTrigger.dbc`, the client's static trigger-volume geometry
// (subzone-entry / exploration / teleport triggers). Vanilla loads the DBC
// but exposes NONE of its geometry to Lua, so map addons (pfQuest, …) ship
// hand-scraped trigger tables. Same value proposition as `GetMapOverlays`:
// live, correct client data, no per-server table drift.
//
// ## Return shape (one info table)
//
//   id           — AreaTrigger.dbc row id
//   mapID        — Map.dbc id (continent 0/1, or an instance map)
//   x, y, z      — continent-space world coordinates of the trigger center
//   radius       — sphere-trigger radius (0 for a box trigger)
//   isBox        — true when the trigger is an oriented box (see below)
//   boxLength / boxWidth / boxHeight / boxYaw — box dimensions + yaw
//                  (all 0 for a pure sphere trigger)
//   areaID       — AreaTable zone id the point falls in (via WorldMapArea);
//                  absent when no zone rect on this map contains the point
//   mapX / mapY  — the trigger's position as a 0..100 map-relative percent
//                  within that zone (the coordinate form map addons draw
//                  with); absent alongside areaID when unresolved
//
// The raw world geometry is authoritative (straight from the DBC); the
// derived `areaID`/`mapX`/`mapY` is the consumable form. Both are provided
// so a consumer can draw immediately or run its own world→map transform.
//
// ## World → map-percent transform
//
// `WorldMapArea.dbc` gives each zone a world-coordinate rect
// (`locTop`/`locBottom` bound world X, `locLeft`/`locRight` bound world Y —
// WoW's world axes: +X north, +Y west). In map space the HORIZONTAL axis
// (mapX) runs off world Y and the VERTICAL axis (mapY) off world X — the
// standard WoW / pfQuest / Astrolabe convention:
//   mapX% = (locLeft - y) / (locLeft - locRight)  * 100   (horizontal)
//   mapY% = (locTop  - x) / (locTop  - locBottom) * 100   (vertical)
// We pick the zone by containment: among WorldMapArea rows on the trigger's
// map with a real (non-continent) areaID whose rect encloses the point, the
// smallest-area rect wins (the tightest zone — a subzone beats its parent).
// The engine's own map-name resolver reads the same rect fields; see
// `Map::Overlays` for the shared WorldMapArea wiring.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "map/Area.h"

#include <cstdint>

namespace Map::AreaTriggers {

namespace {

float FloatField(const uint8_t *rec, int off) {
    return *reinterpret_cast<const float *>(rec + off);
}

// Zone containment + map-percent projection lives in the shared
// `Map::Area::ZonePercent` (taxi nodes use the same transform).

// Builds one trigger's info table and leaves it on the Lua stack top.
// Returns false (pushing nothing) when `id` is out of range or the slot
// is empty.
bool PushTriggerInfo(void *L, int id) {
    const uint8_t *rec = DBC::Record(Offsets::VAR_AREATRIGGER_RECORDS,
                                     Offsets::VAR_AREATRIGGER_COUNT,
                                     static_cast<uint32_t>(id));
    if (rec == nullptr)
        return false;

    const int mapID = Game::Read<int>(rec, Offsets::OFF_AT_MAP_ID);
    const float x = FloatField(rec, Offsets::OFF_AT_X);
    const float y = FloatField(rec, Offsets::OFF_AT_Y);
    const float z = FloatField(rec, Offsets::OFF_AT_Z);
    const float radius = FloatField(rec, Offsets::OFF_AT_RADIUS);
    const float boxLength = FloatField(rec, Offsets::OFF_AT_BOX_LENGTH);
    const float boxWidth = FloatField(rec, Offsets::OFF_AT_BOX_WIDTH);
    const float boxHeight = FloatField(rec, Offsets::OFF_AT_BOX_HEIGHT);
    const float boxYaw = FloatField(rec, Offsets::OFF_AT_BOX_YAW);
    const bool isBox = (boxLength != 0.0f || boxWidth != 0.0f || boxHeight != 0.0f);

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "id", id);
    Game::Lua::SetFieldNumber(L, "mapID", mapID);
    Game::Lua::SetFieldNumber(L, "x", x);
    Game::Lua::SetFieldNumber(L, "y", y);
    Game::Lua::SetFieldNumber(L, "z", z);
    Game::Lua::SetFieldNumber(L, "radius", radius);
    Game::Lua::SetFieldBool(L, "isBox", isBox);
    Game::Lua::SetFieldNumber(L, "boxLength", boxLength);
    Game::Lua::SetFieldNumber(L, "boxWidth", boxWidth);
    Game::Lua::SetFieldNumber(L, "boxHeight", boxHeight);
    Game::Lua::SetFieldNumber(L, "boxYaw", boxYaw);

    int areaID = 0;
    double mapX = 0.0, mapY = 0.0;
    if (Map::Area::ZonePercent(mapID, x, y, &areaID, &mapX, &mapY)) {
        Game::Lua::SetFieldNumber(L, "areaID", areaID);
        Game::Lua::SetFieldNumber(L, "mapX", mapX);
        Game::Lua::SetFieldNumber(L, "mapY", mapY);
    }
    return true;
}

// `C_Map.GetAreaTriggerInfo(triggerID)` — one trigger's info table, or nil
// for a missing / out-of-range id.
int __fastcall Script_GetAreaTriggerInfo(void *L) {
    const bool hasID = Game::Lua::IsNumber(L, 1);
    const int id = hasID ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    Game::Lua::SetTop(L, 0);
    if (!hasID || !PushTriggerInfo(L, id))
        Game::Lua::PushNil(L);
    return 1;
}

// `C_Map.GetAreaTriggers([mapID])` — array of every trigger's info table,
// optionally filtered to a single Map.dbc id.
int __fastcall Script_GetAreaTriggers(void *L) {
    const bool filter = Game::Lua::IsNumber(L, 1);
    const int wantMap = filter ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const int count = Game::Read<int>(Offsets::VAR_AREATRIGGER_COUNT);
    int outIdx = 0;
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_AREATRIGGER_RECORDS,
                                         Offsets::VAR_AREATRIGGER_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        if (filter &&
            Game::Read<int>(rec, Offsets::OFF_AT_MAP_ID) != wantMap)
            continue;

        outIdx += 1;
        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        PushTriggerInfo(L, id); // rec is valid, so this always pushes a table
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

const Game::Doc::Field kAreaTriggerInfoFields[] = {
    Game::Doc::Req("id", "number"),
    Game::Doc::Req("mapID", "number", "The continent or instance map the trigger is on."),
    Game::Doc::Req("x", "number", "The world X coordinate of the center; X runs north."),
    Game::Doc::Req("y", "number", "The world Y coordinate of the center; Y runs west."),
    Game::Doc::Req("z", "number", "The world height of the center."),
    Game::Doc::Req("radius", "number", "0 for a box trigger."),
    Game::Doc::Req("isBox", "bool", "True when the trigger is a box, not a sphere."),
    Game::Doc::Req("boxLength", "number", "0 for a sphere trigger."),
    Game::Doc::Req("boxWidth", "number", "0 for a sphere trigger."),
    Game::Doc::Req("boxHeight", "number", "0 for a sphere trigger."),
    Game::Doc::Req("boxYaw", "number", "0 for a sphere trigger."),
    Game::Doc::Opt("areaID", "number", nullptr,
                   "The zone the trigger is in; nil when no zone map holds the trigger."),
    Game::Doc::Opt("mapX", "number", nullptr,
                   "The horizontal position on the zone map, from 0 to 100; nil with areaID."),
    Game::Doc::Opt("mapY", "number", nullptr,
                   "The vertical position on the zone map, from 0 to 100; nil with areaID."),
};
const Game::Doc::Structure kAreaTriggerInfo{"AreaTriggerInfo", "Map", kAreaTriggerInfoFields,
                                            "A trigger volume in the world and its map position."};

const Game::Doc::Field kTriggerInfoArgs[] = {
    Game::Doc::Req("triggerID", "number"),
};
const Game::Doc::Field kTriggerInfoRets[] = {
    Game::Doc::Opt("info", "AreaTriggerInfo", nullptr, "Nil when no trigger has the ID."),
};
const Game::Doc::Function kGetAreaTriggerInfo{
    "Returns the position and shape of an area trigger.", kTriggerInfoArgs, kTriggerInfoRets,
    nullptr, true};

const Game::Doc::Field kTriggersArgs[] = {
    Game::Doc::Opt("mapID", "number", nullptr,
                   "Limits the list to one continent or instance map."),
};
const Game::Doc::Field kTriggersRets[] = {
    Game::Doc::Req("triggers", "table", "An array of AreaTriggerInfo."),
};
const Game::Doc::Function kGetAreaTriggers{
    "Returns every area trigger, or only the triggers on one map.", kTriggersArgs,
    kTriggersRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "GetAreaTriggerInfo",
                                     &Script_GetAreaTriggerInfo, &kGetAreaTriggerInfo);
    Game::Lua::RegisterTableFunction("C_Map", "GetAreaTriggers",
                                     &Script_GetAreaTriggers, &kGetAreaTriggers);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Map::AreaTriggers
