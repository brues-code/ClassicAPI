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

#pragma once

#include <cstdint>

// `Map::Area` — shared `WorldMapArea.dbc` row resolution. A WorldMapArea
// row is the client's per-zone map descriptor (directory name + the
// world-coordinate rect used for world↔map transforms); every C_Map.*
// reader that keys off "which zone" resolves a row first. Both
// `Map::Overlays` (GetMapOverlays) and `Map::WorldSize` (GetMapWorldSize)
// go through here so the two ways of naming a zone — an AreaTable areaID
// argument, or the world map's current continent/zone view — live in one
// place.
namespace Map::Area {

// The world map's detail canvas in pixels (`WorldMapDetailFrame`) and the
// size of one background tile — both taken from the client's own
// `WorldMapFrame.xml`, where the frame is 1002x668 and each of the twelve
// `WorldMapDetailTile` textures is 256x256 (tile 5 anchors below tile 1, so
// the grid is 4 wide by 3 tall). A map's background is that grid, and
// `WorldMapOverlay` placement/hit rects are authored in the same space.
//
// These are deliberately CONSTANTS rather than a live read of
// `WorldMapDetailFrame`, which would look like the more "engine-driven"
// choice and is in fact the wrong one, twice over:
//
//   - The frame is addon-mutable. Map addons resize and rescale it, but the
//     DBC coordinates these normalize don't move — they were authored
//     against the shipped size. Reading the frame live would silently skew
//     every overlay the moment someone scaled the map.
//   - They are LOGICAL sizes, not on-disk ones. An HD map patch ships
//     larger tile files; `Map::Overlays` detects that from the real BLP
//     dimensions and scales. Reporting a patched tile's true size here
//     would break the `canvas / tile` division that yields the 4x3 grid.
//
// So the authored value is the truth, and the runtime one is the mutable
// approximation — the reverse of the usual case.
constexpr double kMapCanvasWidth = 1002.0;
constexpr double kMapCanvasHeight = 668.0;
constexpr int kMapTileSize = 256;

// WorldMapArea row for an AreaTable `areaID` (the stable zone identity
// `C_Map.GetBestMapForUnit` returns). Linear walk of the ~175-row table.
// Returns -1 when no row carries that areaID.
int RowForAreaID(uint32_t areaID);

// WorldMapArea row for the world map's currently *viewed* zone — the same
// continent/zone selection the engine's map-name resolver (FUN_004a6cf0,
// behind GetMapInfo) performs: continent index → per-continent blob, zone
// index (-1 = whole continent) → that continent's zone-row array; no
// continent selected falls back to the default-row global. Returns -1 when
// the continent blob isn't resident.
int CurrentViewRow();

// Resolves world point (x, y) on continent `mapID` to a zone: its AreaTable
// areaID and a 0..100 zone-relative percent (`mapX` horizontal off world Y,
// `mapY` vertical off world X — the WoW/pfQuest convention).
//
// WorldMapArea rects are loose, overlapping bounding boxes, so a point near a
// zone edge falls inside several. Resolution therefore prefers the zone whose
// **drawn landmass** (WorldMapOverlay hit rect) the point actually sits on —
// the runtime analog of an ADT area-grid lookup (Un'Goro's crater vs Thousand
// Needles' empty overlap). Overlay hit rects are themselves rectangles that
// overlap at borders, so among landmass matches the winner is the zone the
// point is DEEPEST inside an overlay of (Nijel's Point is deep in Desolace's
// own overlay but only clips the edge of Stonetalon's Charred Vale box). If the
// point is on no zone's overlay, falls back to the most-interior containing
// rect. Returns false when no zone rect encloses the point (open sea,
// degenerate rect, an instance with no zone row). Shared by GetAreaTriggerInfo
// and the taxi-node zone resolve.
bool ZonePercent(int mapID, float x, float y, int *outAreaID, double *outMapX,
                 double *outMapY);

// Projects world (x, y) into the WorldMapArea rect of a SPECIFIC AreaTable
// zone (`areaID`), filling the 0..100 zone-relative percent (`mapX` horizontal
// off world Y, `mapY` vertical off world X). Returns false when that zone has
// no usable WorldMapArea row OR the point lies outside its rect. Used when the
// zone is already known by other means (a taxi node's name) and only the
// in-zone position is needed — bypassing the containment / landmass search of
// ZonePercent. The containment check lets a caller test candidate zones (a
// node's city vs its outdoor region) and take the first that actually holds
// the point.
bool PercentInZone(int areaID, float x, float y, double *outMapX, double *outMapY);

// Projects world (x, y) on `mapID` to 0..1 CONTINENT-map coordinates using
// the continent's WorldMapArea row (the areaID == 0 row with a non-degenerate
// rect — some maps carry a stray zero-rect continent row that is skipped).
// `outPx` is horizontal (off world Y), `outPy` vertical (off world X).
// Returns false when the map has no usable continent row. This is the
// projection retail's TaxiNodeInfo.position uses.
bool ContinentPercent(int mapID, float x, float y, double *outPx, double *outPy);

// --- uiMapID identity ------------------------------------------------------
//
// A ClassicAPI `uiMapID` names either a zone or a whole map:
//   positive  = an `AreaTable.dbc` area id (a zone) — what
//               `C_Map.GetBestMapForUnit` returns.
//   negative  = -(WorldMapArea row) — continent / instance maps, which carry
//               no AreaTable id (their rows have areaID 0).
//   kWorldUiMapID = the whole-world map.
// The negative half mirrors the engine's own two-namespaces-in-one-int idiom
// (a quest's `zoneOrSort`: positive = AreaTable zone, negative = -QuestSort
// row), so the two id spaces can never collide.
//
// 1.12 has no UiMap table, and its world view has neither an area nor a row:
// the engine's world map is the selection "continent -1" (`SetMapZoom(0)`,
// benilla-re ui/worldmap "The selection"), drawn from
// `Interface\WorldMap\World\`. Stock 1.12.1's WorldMapArea.dbc (51 rows) has
// no world row; Turtle's adds one (694, "World", zero rect), so deriving the
// id from a row worked on Turtle only (issue #157). The id is therefore
// retail's own world uiMapID, 947 — an AreaTable id in neither the stock
// (max 3486) nor the Turtle (max 5735) table, so it cannot name a zone.
constexpr int kWorldUiMapID = 947;

// WorldMapArea row for a `uiMapID` in either namespace. Returns -1 when the
// id resolves to no row, which includes `kWorldUiMapID`.
int RowForUiMapID(int uiMapID);

// The `uiMapID` that names WorldMapArea row `row` — its areaID when it has
// one, else -(row). Returns 0 for an unreadable row.
int UiMapIDForRow(int row);

// The continent-level WorldMapArea row for a `Map.dbc` map: the areaID == 0
// row with a non-degenerate rect (the same selection `ContinentPercent`
// makes, so a stray zero-rect row, such as Turtle's "World" row, never
// wins). -1 when none.
int ContinentRowForMapID(int mapID);

// A WorldMapArea row's world rect. The map's horizontal runs along world Y
// (left edge at the larger Y), its vertical along world X (top edge at the
// larger X) -- the WoW/pfQuest convention.
struct Rect {
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    // A real rect: stray continent rows (Turtle's "World" row) are zero.
    bool Valid() const { return left - right > 0.0 && top - bottom > 0.0; }
    bool Contains(double x, double y) const {
        return x >= bottom && x <= top && y >= right && y <= left;
    }
    // World (x, y) to 0..1 on the rect (no containment gate), and back.
    double Px(double y) const { return (left - y) / (left - right); }
    double Py(double x) const { return (top - x) / (top - bottom); }
    double WorldY(double px) const { return left - px * (left - right); }
    double WorldX(double py) const { return top - py * (top - bottom); }
};

// Reads WorldMapArea row `row`'s rect and its `Map.dbc` map id. Either
// out-parameter may be null. False when the row is unreadable.
bool RowRect(int row, Rect *outRect, int *outMapID);

// Projects world (x, y) into row `row`'s rect as 0..1 (`outPx` horizontal off
// world Y, `outPy` vertical off world X). Unlike `PercentInZone` there is NO
// containment gate — a point outside the rect yields a value outside 0..1, and
// the caller decides whether that is meaningful. False for a missing or
// degenerate rect.
bool PercentInRow(int row, float x, float y, double *outPx, double *outPy);

// Inverse of `PercentInRow`: a 0..1 position on row `row` back to
// continent-space world coordinates. False for a missing or degenerate rect.
bool WorldFromRow(int row, double px, double py, double *outX, double *outY);

} // namespace Map::Area
