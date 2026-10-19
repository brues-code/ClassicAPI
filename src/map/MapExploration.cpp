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

// `C_MapExplorationInfo.GetExploredMapTextures(areaID)` and
// `GetUnexploredMapTextures(areaID)` — the exploration-filtered views of a
// zone's `WorldMapOverlay.dbc` entries.
//
// - `GetExploredMapTextures` is the retail-named backport: only the overlays
//   the local player has **discovered**. Retail keys on `uiMapID` and returns
//   numeric `fileDataIDs`; here it's an `AreaTable` areaID and the tiles are
//   texture paths (`fileDataIDs` holds those paths, see Overlays.cpp).
// - `GetUnexploredMapTextures` is the ClassicAPI-extension complement: the
//   overlays NOT yet discovered — what a map-reveal addon draws over the
//   fogged base map. No retail equivalent (retail only ships the explored
//   getter); its inverse, `C_Map.GetMapOverlays`, returns *all* overlays.
//
// All three share `Map::Overlays::PushZoneOverlays` — same per-overlay table
// shape (texture, resolved tiles, hit rect, areaIDs, fileDataIDs); these two
// just pass an exploration Filter. "Explored" is the engine's own per-area
// test (see Overlays.cpp `IsOverlayExplored`), so the areaID argument works
// for any zone, not just the one on screen.
//
// With no argument, both default to the world map's currently viewed zone
// (like `GetMapOverlays()`).
//
// `MAP_EXPLORATION_UPDATED` (no payload) fires when the player's explored
// areas change. It co-hooks the engine's own explored-bits watcher
// (FUN_EXPLORED_BITS_CHANGED): a descriptor observer the engine arms on
// EnterWorldUI and clears on LeaveWorldUI, which the dispatcher calls only
// when the explored bytes really changed. The event therefore has the
// engine's exact timing and lifetime with no observer of our own. It fires
// after the original, so the engine's map rebuild (and its WORLD_MAP_UPDATE)
// has already run and both getters above see the new state.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "dbc/Lookup.h"
#include "map/Area.h"
#include "map/Overlays.h"
#include "map/Vector2.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Map::Exploration {

namespace {

const Game::Doc::Event kExplorationUpdatedDoc{
    "MapExplorationInfo",
    "Fires when the player explores a new area.",
    {}};
const Event::Custom::AutoReserve _rUpdated{"MAP_EXPLORATION_UPDATED",
                                           &kExplorationUpdatedDoc};

// Observer callback ABI per the FUN_DESC_OBSERVER_REGISTER block in
// Offsets.h: __fastcall(fieldOffset /*ecx*/, size /*edx*/, guidLo, guidHi,
// oldValues*, userArg), returns 1, callee cleans 0x10.
using ExploredBitsChanged_t = int(__fastcall *)(uint32_t fieldOffset,
                                                uint32_t size, uint32_t guidLo,
                                                uint32_t guidHi,
                                                const void *oldValues,
                                                void *userArg);
ExploredBitsChanged_t g_origExploredBitsChanged = nullptr;

int __fastcall ExploredBitsChanged_h(uint32_t fieldOffset, uint32_t size,
                                     uint32_t guidLo, uint32_t guidHi,
                                     const void *oldValues, void *userArg) {
    const int rc = g_origExploredBitsChanged(fieldOffset, size, guidLo, guidHi,
                                             oldValues, userArg);
    Event::Custom::Fire(_rUpdated.Slot(), "");
    return rc;
}

const Game::HookAutoRegister _exploredHook{
    Offsets::FUN_EXPLORED_BITS_CHANGED,
    reinterpret_cast<void *>(&ExploredBitsChanged_h),
    reinterpret_cast<void **>(&g_origExploredBitsChanged)};

int ResolveRow(void *L) {
    return Game::Lua::IsNumber(L, 1)
               ? Map::Area::RowForAreaID(
                     static_cast<uint32_t>(Game::Lua::ToNumber(L, 1)))
               : Map::Area::CurrentViewRow();
}

int __fastcall Script_GetExploredMapTextures(void *L) {
    const int row = ResolveRow(L);
    Game::Lua::SetTop(L, 0);
    Map::Overlays::PushZoneOverlays(L, row, Map::Overlays::Filter::Explored);
    return 1;
}

int __fastcall Script_GetUnexploredMapTextures(void *L) {
    const int row = ResolveRow(L);
    Game::Lua::SetTop(L, 0);
    Map::Overlays::PushZoneOverlays(L, row, Map::Overlays::Filter::Unexplored);
    return 1;
}

// An overlay counts at a point when its art covers at least half the pixel:
// the blended pixel is then mostly the revealed art rather than the fogged
// base map under it.
constexpr int kArtCoverage = 128;

// `C_MapExplorationInfo.GetExploredAreaIDsAtPosition(uiMapID, pos)` — the
// explored areas whose revealed art is under a map point, most visible
// first; nil when there are none. Retail's map labels the cursor with the
// first of them that has a name (MapUtil.FindBestAreaNameAtMouse).
//
// The test is the art itself, not the overlay hit rects: measured offline
// over the 1.12 overlay art, the rects claim an area over bare base map at
// 16.7% of points and give the wrong first area at 11.6% where they
// overlap, while 0.3% of art lies outside its own rect. Overlapping art
// (soft edges, 13.7% of covered points) is ordered by its alpha there; an
// overlay spanning several areas contributes its explored ones in DBC order.
int __fastcall Script_GetExploredAreaIDsAtPosition(void *L) {
    const int row = Game::Lua::IsNumber(L, 1)
                        ? Map::Area::RowForUiMapID(static_cast<int>(Game::Lua::ToNumber(L, 1)))
                        : -1;
    double nx = 0.0, ny = 0.0;
    const bool havePos = Map::ReadVector2D(L, 2, &nx, &ny);
    Game::Lua::SetTop(L, 0);
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (row <= 0 || !havePos || player == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const double cx = nx * Map::Area::kMapCanvasWidth;
    const double cy = ny * Map::Area::kMapCanvasHeight;

    struct Hit {
        int alpha;
        const uint8_t *rec;
    };
    std::vector<Hit> hits;
    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_OVERLAY_COUNT);
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_OVERLAY_RECORDS,
                                         Offsets::VAR_WORLDMAP_OVERLAY_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr || Game::Read<int>(rec, Offsets::OFF_WMO_WORLDMAP_AREA) != row ||
            !Map::Overlays::IsOverlayExplored(rec, player))
            continue;
        const int alpha = Map::Overlays::ArtAlphaAt(rec, cx, cy);
        if (alpha >= kArtCoverage)
            hits.push_back({alpha, rec});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit &a, const Hit &b) { return a.alpha > b.alpha; });

    int n = 0;
    std::vector<int> seen;
    for (const Hit &hit : hits) {
        for (int k = 0; k < 4; ++k) {
            const int areaID = Game::Read<int>(hit.rec, Offsets::OFF_WMO_AREA_ID + k * 4);
            if (!Map::Overlays::IsAreaExplored(areaID, player) ||
                std::find(seen.begin(), seen.end(), areaID) != seen.end())
                continue;
            seen.push_back(areaID);
            if (n == 0)
                Game::Lua::NewTable(L);
            Game::Lua::PushNumber(L, static_cast<double>(++n));
            Game::Lua::PushNumber(L, static_cast<double>(areaID));
            Game::Lua::SetTable(L, -3);
        }
    }
    if (n == 0)
        Game::Lua::PushNil(L);
    return 1;
}

const Game::Doc::Field kAtPositionArgs[] = {
    Game::Doc::Req("uiMapID", "number", "The map to look on."),
    Game::Doc::Req("normalizedPosition", "vector2", "The point on the map, from 0 to 1 on each axis."),
};
const Game::Doc::Field kAtPositionRets[] = {
    Game::Doc::Opt("areaIDs", "table", nullptr,
                   "The explored areas whose map art is at the point, most visible first; nil when there are none."),
};
const Game::Doc::Function kGetExploredAreaIDsAtPosition{
    "Returns the explored areas at a point on a map.", kAtPositionArgs, kAtPositionRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_MapExplorationInfo",
                                     "GetExploredAreaIDsAtPosition",
                                     &Script_GetExploredAreaIDsAtPosition,
                                     &kGetExploredAreaIDsAtPosition);
    Game::Lua::RegisterTableFunction("C_MapExplorationInfo",
                                     "GetExploredMapTextures",
                                     &Script_GetExploredMapTextures);
    Game::Lua::RegisterTableFunction("C_MapExplorationInfo",
                                     "GetUnexploredMapTextures",
                                     &Script_GetUnexploredMapTextures);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Map::Exploration
