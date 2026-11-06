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

// `C_TaxiMap.GetTaxiNodesForMap(mapID)` (static) and
// `C_TaxiMap.GetAllTaxiNodes([uiMapID])` (live) — backports of the retail
// taxi-map node enumerators, plus the `Enum.FlightPathFaction` /
// `Enum.FlightPathState` tables so retail code ports unchanged.
//
// - **GetTaxiNodesForMap** reads `TaxiNodes.dbc` directly: every flight point
//   on a continent (or all continents with no arg), faction-tagged,
//   regardless of discovery — the static DB a zone-map addon (pfQuest's
//   `pfDB.meta.flight`) wants. No flight master needed. Non-flight rows are filtered out — the DBC also holds transports/
//   boats/zeppelins (no flight mount) and non-selectable markers like
//   Northshire Abbey (no connecting flight path); a real flight master has a
//   mount AND is a TaxiPath endpoint.
// - **GetAllTaxiNodes** wraps the open flight master's live reachable-node
//   list (the legacy NumTaxiNodes/TaxiNode* globals' backing state): the
//   nodes you can actually fly to right now, with per-node reachability and
//   the slot index `TakeTaxiNode` takes. Empty when no taxi map is open.
//
// Ship both: static/DBC for databases + overlays, live/session for a custom
// flight-map UI. See docs/API.md for the field-by-field contract and which
// fields are retail vs ClassicAPI extensions.

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "dbc/Lookup.h"
#include "dbc/Names.h"
#include "map/Area.h"

#include <cstdint>
#include <cstring>

namespace Taxi::Map {

namespace {

// Enum.FlightPathFaction (retail values).
enum { FACTION_NEUTRAL = 0, FACTION_HORDE = 1, FACTION_ALLIANCE = 2 };
// Enum.FlightPathState (retail values).
enum { STATE_CURRENT = 0, STATE_REACHABLE = 1, STATE_UNREACHABLE = 2 };

float FloatField(const uint8_t *rec, int off) {
    return *reinterpret_cast<const float *>(rec + off);
}
int IntField(const uint8_t *rec, int off) {
    return *reinterpret_cast<const int *>(rec + off);
}

// --- name-based zone resolution -------------------------------------------
//
// A taxi node's name is "Location, Zone" (e.g. "Chillwind Camp, Western
// Plaguelands"), so the zone is authoritative in the data itself — which
// beats any geometric world->zone guess at genuine border interleaving that
// the WorldMapArea/overlay boxes can't separate (Chillwind sits by Strahnbrad,
// deep in Alterac's overlay box, but is a WPL node). We resolve the name
// suffix against AreaTable and fall back to the geometric resolver only when a
// node has no ", Zone" suffix or the zone name doesn't match.

// True if `name` begins with `prefix` followed by a space — a word-prefix, so
// the abbreviated taxi suffixes ("Redridge", "Arathi") match the full
// AreaTable names ("Redridge Mountains", "Arathi Highlands").
bool IStartsWord(const char *name, const char *prefix) {
    while (*prefix) {
        if (!*name || Ascii::ToLower(*name) != Ascii::ToLower(*prefix))
            return false;
        ++name;
        ++prefix;
    }
    return *name == ' ';
}

// Pointer into `name` at the zone part (after the last ", "), or null when the
// name has no ", " separator.
const char *ZoneSuffix(const char *name) {
    const char *best = nullptr;
    if (name != nullptr)
        for (const char *p = name; p[0] != '\0'; ++p)
            if (p[0] == ',' && p[1] == ' ')
                best = p + 2;
    return best;
}

// Copies the location part (before the FIRST ", ") of `name` into `out`,
// null-terminated. Returns `out`, or null when `name` has no ", " separator.
// This is the "Orgrimmar" in "Orgrimmar, Durotar" — a capital's own city map
// when it's a mapped area, distinct from its outdoor region.
const char *LocationPrefix(const char *name, char *out, int outSize) {
    if (name == nullptr)
        return nullptr;
    for (int i = 0; name[i] != '\0'; ++i) {
        if (name[i] == ',' && name[i + 1] == ' ') {
            int n = (i < outSize - 1) ? i : outSize - 1;
            for (int j = 0; j < n; ++j)
                out[j] = name[j];
            out[n] = '\0';
            return out;
        }
    }
    return nullptr;
}

// AreaTable areaID for a zone name: exact (case-insensitive) match preferred,
// else the first word-prefix match. 0 when nothing matches.
int ResolveZoneByName(const char *zone) {
    if (zone == nullptr || *zone == '\0')
        return 0;
    const int count =
        Game::Read<int>(Offsets::VAR_AREATABLE_COUNT);
    int prefixHit = 0;
    for (int id = 1; id <= count; ++id) {
        const char *nm =
            DBC::AreaName(static_cast<uint32_t>(id), /*resolveToParent=*/false);
        if (nm == nullptr)
            continue;
        if (Ascii::EqualCI(nm, zone))
            return id; // exact wins outright
        if (prefixHit == 0 && IStartsWord(nm, zone))
            prefixHit = id;
    }
    return prefixHit;
}

// Pushes { x = px, y = py } as a subtable field `key` of the table on top.
void PushPosition(void *L, const char *key, double px, double py) {
    Game::Lua::PushString(L, key);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "x", px);
    Game::Lua::SetFieldNumber(L, "y", py);
    Game::Lua::SetTable(L, -3);
}

// Walks TaxiPath.dbc into two per-node-id flags:
//   endpoint[] — the node is either end of some flight path. A real flight
//                master always is; the fake rows (Northshire Abbey, spurious
//                duplicate hub rows, standalone markers like "Eastern
//                Plaguelands"/"Naxxramas") connect to nothing.
//   dest[]     — the node is a path DESTINATION (in-degree > 0), i.e. you can
//                fly TO it. In-degree 0 marks a departure-only node reachable
//                only by other means: druid Teleport (Nighthaven, Moonglade),
//                a boat dock (Stormwind Harbor), etc.
// ids >= `size` are ignored (the vanilla table maxes ~122, so the fixed buffer
// is ample). Returns false when the path table isn't loaded.
constexpr int kMaxTaxiNodeID = 2048;
bool BuildTaxiGraph(bool *endpoint, bool *dest, int size) {
    const int count = Game::Read<int>(Offsets::VAR_TAXIPATH_COUNT);
    if (count <= 0)
        return false;
    for (int i = 1; i <= count; ++i) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_TAXIPATH_RECORDS,
                                         Offsets::VAR_TAXIPATH_COUNT,
                                         static_cast<uint32_t>(i));
        if (rec == nullptr)
            continue;
        const int from = IntField(rec, Offsets::OFF_TAXIPATH_FROM);
        const int to = IntField(rec, Offsets::OFF_TAXIPATH_TO);
        if (from >= 0 && from < size)
            endpoint[from] = true;
        if (to >= 0 && to < size) {
            endpoint[to] = true;
            dest[to] = true;
        }
    }
    return true;
}

// `C_TaxiMap.GetTaxiNodesForMap([mapID])` — see module comment. A numeric
// `mapID` filters to that continent; omitted / non-number returns every
// flight master on every continent.
int __fastcall Script_GetTaxiNodesForMap(void *L) {
    const bool filterMap = Game::Lua::IsNumber(L, 1);
    const int wantMap =
        filterMap ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : -1;

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    // Real flight masters are TaxiPath endpoints; `dest` marks the ones a
    // path leads to (in-degree > 0) for the `reachable` field.
    bool endpoint[kMaxTaxiNodeID] = {};
    bool dest[kMaxTaxiNodeID] = {};
    BuildTaxiGraph(endpoint, dest, kMaxTaxiNodeID);

    const int count =
        Game::Read<int>(Offsets::VAR_TAXINODES_COUNT);
    int outIdx = 0;
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_TAXINODES_RECORDS,
                                         Offsets::VAR_TAXINODES_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        const int mapID = IntField(rec, Offsets::OFF_TAXINODE_MAP_ID);
        if (filterMap && mapID != wantMap)
            continue;

        // Two filters isolate real flight masters from the DBC's non-flight
        // rows: (1) at least one flight mount — drops transports/boats/
        // zeppelins/instance markers (both mounts 0); (2) the node is a
        // TaxiPath endpoint — drops Northshire Abbey, spurious duplicate hub
        // rows, and standalone markers that connect to no flight path.
        const int horde = IntField(rec, Offsets::OFF_TAXINODE_HORDE_MOUNT);
        const int alliance = IntField(rec, Offsets::OFF_TAXINODE_ALLIANCE_MOUNT);
        if (horde == 0 && alliance == 0)
            continue;
        if (id >= kMaxTaxiNodeID || !endpoint[id])
            continue;

        const char *name = DBC::LocalizedField(Offsets::VAR_TAXINODES_RECORDS,
                                               Offsets::VAR_TAXINODES_COUNT,
                                               static_cast<uint32_t>(id),
                                               Offsets::OFF_TAXINODE_NAME);
        // The one path-connected, mounted row that still isn't a flight
        // master: Blizzard's internal "Generic, World target" zeppelin-route
        // marker. It's the sole such placeholder across all continents.
        if (name != nullptr && std::strncmp(name, "Generic", 7) == 0)
            continue;

        const int faction = (horde != 0 && alliance != 0) ? FACTION_NEUTRAL
                            : (alliance != 0)              ? FACTION_ALLIANCE
                                                           : FACTION_HORDE;

        const float x = FloatField(rec, Offsets::OFF_TAXINODE_X);
        const float y = FloatField(rec, Offsets::OFF_TAXINODE_Y);
        const float z = FloatField(rec, Offsets::OFF_TAXINODE_Z);

        outIdx += 1;
        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "nodeID", id);
        Game::Lua::SetFieldString(L, "name", name ? name : "");
        Game::Lua::SetFieldNumber(L, "faction", faction);
        // reachable: a flight path leads to this node (in-degree > 0). False
        // marks a departure-only node reachable only by other means — druid
        // Teleport (Nighthaven, Moonglade) or a boat dock (Stormwind Harbor);
        // hide those from a non-class-specific "where can I fly" map.
        Game::Lua::SetFieldBool(L, "reachable", id < kMaxTaxiNodeID && dest[id]);
        Game::Lua::SetFieldNumber(L, "mapID", mapID);
        Game::Lua::SetFieldNumber(L, "x", x);
        Game::Lua::SetFieldNumber(L, "y", y);
        Game::Lua::SetFieldNumber(L, "z", z);

        // position: 0-1 on the continent map (retail-accurate). Falls back to
        // {0,0} when the continent has no usable WorldMapArea row (instance
        // maps); zone-map addons use areaID + mapX/mapY below instead.
        double px = 0.0, py = 0.0;
        ::Map::Area::ContinentPercent(mapID, x, y, &px, &py);
        PushPosition(L, "position", px, py);

        // Derived zone anchor (areaID + 0-100 zone-relative percent), from the
        // node's own name — authoritative, and right at borders geometry can't
        // separate. Two candidate zones from the name, each accepted only if
        // its WorldMapArea rect actually contains the point:
        //   1. the LOCATION ("Orgrimmar" in "Orgrimmar, Durotar") — a capital's
        //      own city map, so the flight master pins on the city, not the
        //      surrounding region;
        //   2. else the ZONE suffix ("Western Plaguelands") — the outdoor zone
        //      for a normal node (whose location is an unmapped subzone).
        // Falls back to the geometric landmass resolver when neither name part
        // resolves to a containing zone (no ", Zone" suffix, unknown name).
        int areaID = 0;
        double mapX = 0.0, mapY = 0.0;
        bool haveZone = false;
        char locBuf[128];
        const int locArea = ResolveZoneByName(LocationPrefix(name, locBuf, sizeof(locBuf)));
        const int zoneArea = ResolveZoneByName(ZoneSuffix(name));
        if (locArea > 0 && ::Map::Area::PercentInZone(locArea, x, y, &mapX, &mapY)) {
            areaID = locArea; // capital / mapped location -> its own city map
            haveZone = true;
        } else if (zoneArea > 0 &&
                   ::Map::Area::PercentInZone(zoneArea, x, y, &mapX, &mapY)) {
            areaID = zoneArea; // outdoor zone from the name suffix
            haveZone = true;
        } else {
            haveZone = ::Map::Area::ZonePercent(mapID, x, y, &areaID, &mapX, &mapY);
        }
        if (haveZone) {
            Game::Lua::SetFieldNumber(L, "areaID", areaID);
            Game::Lua::SetFieldNumber(L, "mapX", mapX);
            Game::Lua::SetFieldNumber(L, "mapY", mapY);
        }

        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

using TaxiNodeType_t = const char *(__fastcall *)(uint32_t slot0Based);

int StateFromType(const char *type) {
    if (type == nullptr)
        return STATE_UNREACHABLE;
    if (std::strcmp(type, "CURRENT") == 0)
        return STATE_CURRENT;
    if (std::strcmp(type, "REACHABLE") == 0)
        return STATE_REACHABLE;
    return STATE_UNREACHABLE; // DISTANT / NONE / other
}

// `C_TaxiMap.GetAllTaxiNodes([uiMapID])` — see module comment. The uiMapID
// arg is accepted for retail signature parity but ignored (vanilla has a
// single active taxi map — the open flight master's).
int __fastcall Script_GetAllTaxiNodes(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    const int count =
        Game::Read<int>(Offsets::VAR_TAXI_NODE_COUNT);
    if (count <= 0)
        return 1; // no taxi map open

    auto typeFn = reinterpret_cast<TaxiNodeType_t>(Offsets::FUN_TAXI_NODE_TYPE);
    const auto *arrayBase =
        reinterpret_cast<const uint8_t *>(Offsets::VAR_TAXI_NODE_ARRAY);

    for (int slot = 0; slot < count; ++slot) {
        const uint8_t *slotRec = arrayBase + slot * Offsets::TAXI_NODE_STRIDE;
        const uint8_t *dbcRec = Game::Read<const uint8_t *>(
            slotRec, Offsets::OFF_TAXI_SLOT_RECORD);
        const float x = FloatField(slotRec, Offsets::OFF_TAXI_SLOT_X);
        const float y = FloatField(slotRec, Offsets::OFF_TAXI_SLOT_Y);

        int nodeID = 0;
        const char *name = nullptr;
        if (dbcRec != nullptr) {
            nodeID = IntField(dbcRec, 0); // TaxiNodes.dbc id @ +0
            name = DBC::LocalizedField(Offsets::VAR_TAXINODES_RECORDS,
                                       Offsets::VAR_TAXINODES_COUNT,
                                       static_cast<uint32_t>(nodeID),
                                       Offsets::OFF_TAXINODE_NAME);
        }
        const int state = StateFromType(typeFn(static_cast<uint32_t>(slot)));

        Game::Lua::PushNumber(L, static_cast<double>(slot + 1));
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldNumber(L, "slotIndex", slot + 1); // TakeTaxiNode arg
        Game::Lua::SetFieldString(L, "name", name ? name : "");
        Game::Lua::SetFieldNumber(L, "nodeID", nodeID);
        Game::Lua::SetFieldNumber(L, "state", state);
        PushPosition(L, "position", x, y); // flight-map 0-1, matches retail
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

// Direct-edge test — mirrors FUN_004dbad0: a 256x256 [from][to] matrix of
// TaxiPath IDs (0 = no direct flight path). IDs outside 1..256 are never direct.
bool DirectEdge(int from, int to) {
    if (from <= 0 || from > Offsets::TAXI_ADJACENCY_DIM ||
        to <= 0 || to > Offsets::TAXI_ADJACENCY_DIM)
        return false;
    const int *adj = reinterpret_cast<const int *>(Offsets::VAR_TAXI_ADJACENCY);
    return adj[from * Offsets::TAXI_ADJACENCY_DIM + to] != 0;
}

// `C_TaxiMap.GetTaxiRoute(slotIndex)` — the TaxiNodes.dbc ID chain the client
// would fly to the taxi-map slot `slotIndex` (1..NumTaxiNodes), in flight
// order { currentNodeID, ..., destNodeID }. This mirrors TakeTaxiNode's own
// decision (FUN_004dc8d0): a direct hop when a TaxiPath edge exists, otherwise
// the client's precomputed multi-hop route array. It is the exact node list
// TakeTaxiNode would hand the server, so an addon can identify every hop
// without matching flight-map coordinates back to nodes. Returns nothing when
// no taxi map is open, the slot is out of range, it is the current node, or no
// route exists.
int __fastcall Script_GetTaxiRoute(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int slot1 = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int count =
        Game::Read<int>(Offsets::VAR_TAXI_NODE_COUNT);
    if (count <= 0 || slot1 < 1 || slot1 > count)
        return 0;

    const auto *arrayBase =
        reinterpret_cast<const uint8_t *>(Offsets::VAR_TAXI_NODE_ARRAY);
    const uint8_t *slotRec =
        arrayBase + (slot1 - 1) * Offsets::TAXI_NODE_STRIDE;

    const uint8_t *curRec = Game::Read<const uint8_t *>(
        Offsets::VAR_TAXI_CURRENT_REC);
    const uint8_t *dstRec = Game::Read<const uint8_t *>(
        slotRec, Offsets::OFF_TAXI_SLOT_RECORD);
    if (curRec == nullptr || dstRec == nullptr)
        return 0;
    const int currentID = IntField(curRec, 0);
    const int destID = IntField(dstRec, 0);
    if (currentID == 0 || destID == 0 || currentID == destID)
        return 0;

    // Collect the chain exactly as FUN_004dc8d0 (TakeTaxiNode) would send it.
    int ids[64];
    int n = 0;
    if (DirectEdge(currentID, destID)) {
        ids[n++] = currentID;
        ids[n++] = destID;
    } else if (*(slotRec + Offsets::OFF_TAXI_SLOT_MULTIHOP) != 0) {
        const int *route = Game::Read<const int *>(
            slotRec, Offsets::OFF_TAXI_SLOT_ROUTE);
        const int rc = Game::Read<int>(
            slotRec, Offsets::OFF_TAXI_SLOT_ROUTE_COUNT);
        if (route == nullptr || rc < 2)
            return 0;
        const int cap = rc < 64 ? rc : 64;
        for (int i = 0; i < cap; ++i)
            ids[n++] = route[i];
    } else {
        return 0; // reachable node with no computed route (shouldn't happen)
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    for (int i = 0; i < n; ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        Game::Lua::PushNumber(L, static_cast<double>(ids[i]));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

void RegisterLuaFunctions() {
    static const Game::Lua::EnumIntegerEntry kFaction[] = {
        {"Neutral", FACTION_NEUTRAL},
        {"Horde", FACTION_HORDE},
        {"Alliance", FACTION_ALLIANCE},
    };
    Game::Lua::RegisterIntegerEnum("Enum", "FlightPathFaction", kFaction);

    static const Game::Lua::EnumIntegerEntry kState[] = {
        {"Current", STATE_CURRENT},
        {"Reachable", STATE_REACHABLE},
        {"Unreachable", STATE_UNREACHABLE},
    };
    Game::Lua::RegisterIntegerEnum("Enum", "FlightPathState", kState);

    Game::Lua::RegisterTableFunction("C_TaxiMap", "GetTaxiNodesForMap",
                                     &Script_GetTaxiNodesForMap);
    Game::Lua::RegisterTableFunction("C_TaxiMap", "GetAllTaxiNodes",
                                     &Script_GetAllTaxiNodes);
    Game::Lua::RegisterTableFunction("C_TaxiMap", "GetTaxiRoute",
                                     &Script_GetTaxiRoute);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Taxi::Map
