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

// `C_Map.GetMapWorldSize([areaID])` — the world size (in yards) of a zone,
// as `width, height`. This is the physical extent the map represents: the
// span of the `WorldMapArea.dbc` placement rect. Modern addons use it to
// convert map-relative percents to yard distances (e.g. tracking-range
// rings, "X yards away" readouts).
//
// With a numeric `areaID` (AreaTable id — the same identity
// `C_Map.GetBestMapForUnit` returns and `C_Map.GetMapOverlays` accepts),
// returns that zone's size. With no argument, returns the world map's
// currently viewed zone (same resolution `GetMapOverlays()` uses with no
// arg). Returns nothing (→ nil, nil) when the zone can't be resolved.
//
// WoW's world axes are +X north, +Y west; the WorldMapArea rect bounds
// them as locTop/locBottom (world X) and locLeft/locRight (world Y). Map
// width is the horizontal (east-west) extent and height the vertical
// (north-south):
//   width  = locLeft - locRight   (world Y span)
//   height = locTop  - locBottom  (world X span)
// The same spans are the denominators in the world→map percent transform
// (see `Map::AreaTriggers`), so a point's `mapX% * width / 100` is its
// offset in yards from the zone's east edge, and likewise for height.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "map/Area.h"

#include <cstdint>

namespace Map::WorldSize {

namespace {

int __fastcall Script_GetMapWorldSize(void *L) {
    const int row = Game::Lua::IsNumber(L, 1)
                        ? Map::Area::RowForAreaID(
                              static_cast<uint32_t>(Game::Lua::ToNumber(L, 1)))
                        : Map::Area::CurrentViewRow();

    Game::Lua::SetTop(L, 0);
    if (row <= 0)
        return 0; // no values -> nil, nil

    const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                     Offsets::VAR_WORLDMAP_AREA_COUNT,
                                     static_cast<uint32_t>(row));
    if (rec == nullptr)
        return 0;

    const float left = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_LEFT);
    const float right = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_RIGHT);
    const float top = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_TOP);
    const float bottom = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_BOTTOM);

    Game::Lua::PushNumber(L, static_cast<double>(left) - right);   // width  (world Y span)
    Game::Lua::PushNumber(L, static_cast<double>(top) - bottom);   // height (world X span)
    return 2;
}

const Game::Doc::Field kWorldSizeArgs[] = {
    Game::Doc::Opt("areaID", "number", nullptr,
                   "The zone; defaults to the zone the world map shows."),
};
const Game::Doc::Field kWorldSizeRets[] = {
    Game::Doc::Opt("width", "number", nullptr,
                   "The east-west size in yards; nil, with height, when the zone has no map."),
    Game::Doc::Opt("height", "number", nullptr, "The north-south size in yards."),
};
const Game::Doc::Function kGetMapWorldSize{"Returns the size of a zone map in yards.",
                                           kWorldSizeArgs, kWorldSizeRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Map", "GetMapWorldSize",
                                     &Script_GetMapWorldSize, &kGetMapWorldSize);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Map::WorldSize
