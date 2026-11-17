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

// The combat log file's MAP_CHANGE line, as Classic Era writes it:
//   MAP_CHANGE,uiMapID,"name",maxX,minX,maxY,minY
// the world map the player is on, its name and its world bounds, each time
// that map changes. Log readers draw positions on it.
//
// 1.12 has no UiMap tables, so the maps are Era's, from its 1.15.9 data
// (UiMap and UiMapAssignment, build 70576): each System 0 continent, zone,
// city and battleground map, with its assignment's map, zone and region box.
// The bounds are that box as Era writes it (Thunder Bluff's
// -849.999939,-1545.833252,516.666626,-527.083313 is its max X, min X, max Y
// and min Y to the digit). The name is 1.12's own, in the client's locale:
// AreaTable's for a zone, Map.dbc's for a continent. All 51 match the UiMap
// names in English.
//
// Which map: the player's zone (VAR_PLAYER_AREA_ID) on the map its AreaTable
// row is on; that zone's map, else the continent's (zone 0), else none. Era
// writes the line when the result changes, "none" included. Checked on its 14
// raid logs: every ZONE_CHANGE into the open world has one (34), Blackrock
// Mountain gives Eastern Kingdoms, and the raids give none, except Onyxia's
// Lair, whose AreaTable row is on Kalimdor (map 1) and which gave Kalimdor
// when entered from Thunder Bluff; leaving AQ40 for Silithus writes Silithus
// again each time. A new log file writes its map again.

#include "Game.h"
#include "Offsets.h"
#include "combatlog/File.h"
#include "dbc/Lookup.h"
#include "object/Resolve.h"
#include "tick/FrameTick.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace CombatLog::MapChange {

namespace {

struct UiMap {
    int map;
    uint32_t zone; // 0 for the continent
    int uiMapID;
    float minX, minY, maxX, maxY;
};

const UiMap kMaps[] = {
    {0, 0, 1415, -16000.0f, -19199.900390625f, 7466.6000976562f, 16000.0f}, // Eastern Kingdoms
    {0, 1, 1426, -7160.4165039062f, -3122.9165039062f, -3877.0832519531f, 1802.0832519531f}, // Dun Morogh
    {0, 3, 1418, -7547.9165039062f, -4566.6665039062f, -5889.5830078125f, -2079.1665039062f}, // Badlands
    {0, 4, 1419, -12800.0f, -4591.6665039062f, -10566.666015625f, -1241.6666259766f}, // Blasted Lands
    {0, 8, 1435, -11150.0f, -4516.6665039062f, -9620.8330078125f, -2222.9165039062f}, // Swamp of Sorrows
    {0, 10, 1431, -11516.666015625f, -1866.6666259766f, -9716.666015625f, 833.33331298828f}, // Duskwood
    {0, 11, 1437, -4904.1665039062f, -4525.0f, -2147.9165039062f, -389.58331298828f}, // Wetlands
    {0, 12, 1429, -10254.166015625f, -1935.4166259766f, -7939.5830078125f, 1535.4166259766f}, // Elwynn Forest
    {0, 28, 1422, 499.99996948242f, -3883.3332519531f, 3366.6665039062f, 416.66665649414f}, // Western Plaguelands
    {0, 33, 1434, -15422.916015625f, -4160.4165039062f, -11168.75f, 2220.8332519531f}, // Stranglethorn Vale
    {0, 36, 1416, -366.66665649414f, -2016.6666259766f, 1500.0f, 783.33331298828f}, // Alterac Mountains
    {0, 38, 1432, -6327.0830078125f, -4752.0830078125f, -4487.5f, -1993.7498779297f}, // Loch Modan
    {0, 40, 1436, -11733.333007812f, -483.33331298828f, -9400.0f, 3016.6665039062f}, // Westfall
    {0, 41, 1430, -11533.333007812f, -3333.3332519531f, -9866.666015625f, -833.33331298828f}, // Deadwind Pass
    {0, 44, 1433, -10022.916015625f, -3741.6665039062f, -8575.0f, -1570.8332519531f}, // Redridge Mountains
    {0, 45, 1417, -2533.3332519531f, -4466.6665039062f, -133.33332824707f, -866.66662597656f}, // Arathi Highlands
    {0, 46, 1428, -8983.3330078125f, -3195.8332519531f, -7031.2495117188f, -266.66665649414f}, // Burning Steppes
    {0, 47, 1425, -1100.0f, -5425.0f, 1466.6666259766f, -1575.0f}, // The Hinterlands
    {0, 51, 1427, -7587.4995117188f, -2554.1665039062f, -6100.0f, -322.91665649414f}, // Searing Gorge
    {0, 85, 1420, 824.99993896484f, -1485.4166259766f, 3837.4997558594f, 3033.3332519531f}, // Tirisfal Glades
    {0, 130, 1421, -1133.3332519531f, -750.0f, 1666.6666259766f, 3449.9997558594f}, // Silverpine Forest
    {0, 139, 1423, 1218.75f, -6056.25f, 3799.9997558594f, -2185.4165039062f}, // Eastern Plaguelands
    {0, 267, 1424, -1733.3332519531f, -2133.3332519531f, 400.0f, 1066.6666259766f}, // Hillsbrad Foothills
    {0, 1497, 1458, 1237.8411865234f, -86.18240356445f, 1877.9453125f, 873.19262695313f}, // Undercity
    {0, 1519, 1453, -9175.205078125f, 36.70063018799f, -8278.8505859375f, 1380.9714355469f}, // Stormwind City
    {0, 1537, 1455, -5096.845703125f, -1504.2164306641f, -4569.2412109375f, -713.59136962891f}, // Ironforge
    {1, 0, 1414, -11733.299804688f, -19733.2109375f, 12799.900390625f, 17066.599609375f}, // Kalimdor
    {1, 14, 1411, -1716.6666259766f, -7249.9995117188f, 1808.3332519531f, -1962.4998779297f}, // Durotar
    {1, 15, 1445, -5533.3330078125f, -6225.0f, -2033.3332519531f, -974.99993896484f}, // Dustwallow Marsh
    {1, 16, 1447, 1960.4166259766f, -8347.916015625f, 5341.6665039062f, -3277.0832519531f}, // Azshara
    {1, 17, 1413, -5143.75f, -7510.4165039062f, 1612.4998779297f, 2622.9165039062f}, // The Barrens
    {1, 141, 1438, 8437.5f, -1277.0832519531f, 11831.25f, 3814.5832519531f}, // Teldrassil
    {1, 148, 1439, 3966.6665039062f, -3608.3332519531f, 8333.3330078125f, 2941.6665039062f}, // Darkshore
    {1, 215, 1412, -3697.9165039062f, -3089.5832519531f, -272.91665649414f, 2047.9166259766f}, // Mulgore
    {1, 331, 1440, 829.16662597656f, -4066.6665039062f, 4672.9165039062f, 1699.9998779297f}, // Ashenvale
    {1, 357, 1444, -6999.9995117188f, -1508.3332519531f, -2366.6665039062f, 5441.6665039062f}, // Feralas
    {1, 361, 1448, 3299.9997558594f, -4108.3330078125f, 7133.3330078125f, 1641.6666259766f}, // Felwood
    {1, 400, 1441, -6899.9995117188f, -4833.3330078125f, -3966.6665039062f, -433.33331298828f}, // Thousand Needles
    {1, 405, 1443, -2545.8332519531f, -262.5f, 452.08331298828f, 4233.3330078125f}, // Desolace
    {1, 406, 1442, -339.58331298828f, -1637.4998779297f, 2916.6665039062f, 3245.8332519531f}, // Stonetalon Mountains
    {1, 440, 1446, -10475.0f, -7118.7495117188f, -5875.0f, -218.74998474121f}, // Tanaris
    {1, 490, 1449, -8433.3330078125f, -3166.6665039062f, -5966.6665039062f, 533.33331298828f}, // Un'Goro Crater
    {1, 493, 1450, 6952.0830078125f, -3689.5832519531f, 8491.666015625f, -1381.25f}, // Moonglade
    {1, 618, 1452, 3799.9997558594f, -7416.6665039062f, 8533.3330078125f, -316.66665649414f}, // Winterspring
    {1, 1377, 1451, -8281.25f, -945.833984375f, -5958.333984375f, 2537.5f}, // Silithus
    {1, 1637, 1454, 1338.4605712891f, -5083.2055664062f, 2273.8771972656f, -3680.6010742188f}, // Orgrimmar
    {1, 1638, 1456, -1545.8332519531f, -527.08331298828f, -849.99993896484f, 516.66662597656f}, // Thunder Bluff
    {1, 1657, 1457, 9532.5869140625f, 1880.0295410156f, 10238.31640625f, 2938.3627929688f}, // Darnassus
    {30, 2597, 1459, -1739.5832519531f, -2456.25f, 1085.4166259766f, 1781.2498779297f}, // Alterac Valley
    {489, 3277, 1460, 862.49993896484f, 895.83331298828f, 1627.0832519531f, 2041.6666259766f}, // Warsong Gulch
    {529, 3358, 1461, 337.5f, 102.08332824707f, 1508.3332519531f, 1858.3332519531f}, // Arathi Basin
};

const UiMap *Find(int map, uint32_t zone) {
    for (const UiMap &m : kMaps)
        if (m.map == map && m.zone == zone)
            return &m;
    return nullptr;
}

// The player's map, or null for none.
const UiMap *Current() {
    const uint32_t zone = Game::Read<uint32_t>(Offsets::VAR_PLAYER_AREA_ID) & 0xFFFF;
    const uint8_t *row = DBC::Record(Offsets::VAR_AREATABLE_RECORDS, Offsets::VAR_AREATABLE_COUNT, zone);
    if (row == nullptr)
        return nullptr;
    const int map = Game::Read<int>(row, Offsets::OFF_AREATABLE_MAP_ID);
    const UiMap *found = Find(map, zone);
    return found != nullptr ? found : Find(map, 0);
}

const char *Name(const UiMap &m) {
    return m.zone != 0 ? DBC::LocalizedField(Offsets::VAR_AREATABLE_RECORDS, Offsets::VAR_AREATABLE_COUNT,
                                             m.zone, Offsets::OFF_AREATABLE_NAMES)
                       : DBC::LocalizedField(Offsets::VAR_MAP_RECORDS, Offsets::VAR_MAP_COUNT,
                                             static_cast<uint32_t>(m.map), Offsets::OFF_MAP_NAME);
}

void Write(const UiMap &m) {
    char text[32];
    std::snprintf(text, sizeof text, "MAP_CHANGE,%d", m.uiMapID);
    std::string line = text;
    File::AppendQuoted(line, Name(m));
    for (const float v : {m.maxX, m.minX, m.maxY, m.minY}) {
        char value[32];
        std::snprintf(value, sizeof value, ",%f", static_cast<double>(v));
        line += value;
    }
    File::WriteLine(line);
}

uint32_t g_session = 0;        // the log file the last line went to
const UiMap *g_last = nullptr; // the map it named

void OnFrame() {
    const uint32_t session = File::Session();
    if (session == 0 || !::Object::RegistryReady())
        return;
    const UiMap *map = Current();
    if (session == g_session && map == g_last)
        return;
    if (map != nullptr)
        Write(*map);
    g_session = session;
    g_last = map;
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

} // namespace

} // namespace CombatLog::MapChange
