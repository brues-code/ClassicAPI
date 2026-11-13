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

// `C_TaxiMap.GetTaxiPaths()` and `C_TaxiMap.GetTaxiPathWaypoints(pathID)` —
// direct readers for `TaxiPath.dbc` (the directed flight-path edge list with
// costs) and `TaxiPathNode.dbc` (each path's world-coordinate waypoint
// polyline). Vanilla exposes neither; addons can't read DBCs at all, so this
// hands them the raw taxi graph + geometry.
//
// Together they let an addon build the taxi routing graph (nodes + weighted
// directed edges) and measure real flight distances (sum the 3D chords of a
// path's waypoints). Uses for a flight-path database, a map overlay, or a
// travel-time estimator — see the sibling `C_TaxiMap.GetTaxiNodesForMap` for
// the node table these edges connect.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Taxi::Paths {

namespace {

int IntField(const uint8_t *rec, int off) {
    return *reinterpret_cast<const int *>(rec + off);
}
float FloatField(const uint8_t *rec, int off) {
    return *reinterpret_cast<const float *>(rec + off);
}

// `C_TaxiMap.GetTaxiPaths()` — every row of TaxiPath.dbc as
// { pathID, fromNodeID, toNodeID, cost }. Directed: A->B and B->A are two
// separate entries.
const Game::Doc::Field kTaxiPathFields[] = {
    Game::Doc::Req("pathID", "number", "The ID that GetTaxiPathWaypoints takes."),
    Game::Doc::Req("fromNodeID", "number"),
    Game::Doc::Req("toNodeID", "number"),
    Game::Doc::Req("cost", "number", "In copper."),
};
const Game::Doc::Structure kTaxiPathInfo{"TaxiPathInfo", "TaxiMap", kTaxiPathFields,
                                         "One flight path, in one direction."};

const Game::Doc::Field kGetTaxiPathsRets[] = {
    Game::Doc::Req("paths", "table", "Array of TaxiPathInfo."),
};
const Game::Doc::Function kGetTaxiPaths{"Returns every flight path.", {}, kGetTaxiPathsRets,
                                        nullptr, true};

int __fastcall Script_GetTaxiPaths(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const int count = Game::Read<int>(Offsets::VAR_TAXIPATH_COUNT);
    int outIdx = 0;
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_TAXIPATH_RECORDS,
                                         Offsets::VAR_TAXIPATH_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;

        outIdx += 1;
        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "pathID", id);
        Game::Lua::SetFieldNumber(L, "fromNodeID", IntField(rec, Offsets::OFF_TAXIPATH_FROM));
        Game::Lua::SetFieldNumber(L, "toNodeID", IntField(rec, Offsets::OFF_TAXIPATH_TO));
        Game::Lua::SetFieldNumber(L, "cost", IntField(rec, Offsets::OFF_TAXIPATH_COST));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// `C_TaxiMap.GetTaxiPathWaypoints(pathID)` — the path's waypoints as
// { x, y, z, mapID }, ordered by node index (flight order). Returns nothing
// for a missing / unknown pathID or an empty path.
const Game::Doc::Field kTaxiPathWaypointFields[] = {
    Game::Doc::Req("x", "number"),
    Game::Doc::Req("y", "number"),
    Game::Doc::Req("z", "number"),
    Game::Doc::Req("mapID", "number", "The continent."),
};
const Game::Doc::Structure kTaxiPathWaypoint{"TaxiPathWaypoint", "TaxiMap",
                                             kTaxiPathWaypointFields,
                                             "One point of a flight path, in world yards."};

const Game::Doc::Field kGetTaxiPathWaypointsArgs[] = {
    Game::Doc::Req("pathID", "number"),
};
const Game::Doc::Field kGetTaxiPathWaypointsRets[] = {
    Game::Doc::Opt("waypoints", "table", nullptr,
                   "Array of TaxiPathWaypoint in flight order; nil for an unknown path."),
};
const Game::Doc::Function kGetTaxiPathWaypoints{
    "Returns the points along a flight path.", kGetTaxiPathWaypointsArgs,
    kGetTaxiPathWaypointsRets, nullptr, true};

int __fastcall Script_GetTaxiPathWaypoints(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int wantPath = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (wantPath <= 0)
        return 0;

    struct Waypoint {
        int index;
        float x, y, z;
        int mapID;
    };
    std::vector<Waypoint> pts;
    const int count = Game::Read<int>(Offsets::VAR_TAXIPATHNODE_COUNT);
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_TAXIPATHNODE_RECORDS,
                                         Offsets::VAR_TAXIPATHNODE_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        if (IntField(rec, Offsets::OFF_TAXIPATHNODE_PATH_ID) != wantPath)
            continue;
        pts.push_back({IntField(rec, Offsets::OFF_TAXIPATHNODE_INDEX),
                       FloatField(rec, Offsets::OFF_TAXIPATHNODE_X),
                       FloatField(rec, Offsets::OFF_TAXIPATHNODE_Y),
                       FloatField(rec, Offsets::OFF_TAXIPATHNODE_Z),
                       IntField(rec, Offsets::OFF_TAXIPATHNODE_MAP)});
    }
    if (pts.empty())
        return 0;

    // DBC rows for a path are stored in index order, but sort to guarantee a
    // dense 1-based (ipairs-safe) array in flight order regardless.
    std::sort(pts.begin(), pts.end(),
              [](const Waypoint &a, const Waypoint &b) { return a.index < b.index; });

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    int outIdx = 0;
    for (const Waypoint &p : pts) {
        outIdx += 1;
        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "x", p.x);
        Game::Lua::SetFieldNumber(L, "y", p.y);
        Game::Lua::SetFieldNumber(L, "z", p.z);
        Game::Lua::SetFieldNumber(L, "mapID", p.mapID);
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_TaxiMap", "GetTaxiPaths",
                                     &Script_GetTaxiPaths, &kGetTaxiPaths);
    Game::Lua::RegisterTableFunction("C_TaxiMap", "GetTaxiPathWaypoints",
                                     &Script_GetTaxiPathWaypoints, &kGetTaxiPathWaypoints);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Taxi::Paths
