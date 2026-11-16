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

#include "map/Area.h"

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

namespace Map::Area {

int RowForAreaID(uint32_t areaID) {
    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_AREA_COUNT);
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                         Offsets::VAR_WORLDMAP_AREA_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec != nullptr &&
            Game::Read<uint32_t>(rec, Offsets::OFF_WMA_AREA_ID) == areaID)
            return id;
    }
    return -1;
}

int CurrentViewRow() {
    const int continent =
        Game::Read<int>(Offsets::VAR_WORLDMAP_CONTINENT_INDEX);
    if (continent < 0)
        return Game::Read<int>(Offsets::VAR_WORLDMAP_DEFAULT_AREA_ROW);
    const uint8_t *blob =
        Game::Read<const uint8_t *>(Offsets::VAR_WORLDMAP_CONTINENT_DATA);
    if (blob == nullptr)
        return -1;
    const uint8_t *entry = blob + continent * Offsets::WORLDMAP_CONTINENT_STRIDE;
    const int zone = Game::Read<int>(Offsets::VAR_WORLDMAP_ZONE_INDEX);
    if (zone < 0)
        return *reinterpret_cast<const int *>(entry + 0x04);
    const int *zoneRows = *reinterpret_cast<const int *const *>(entry + 0x10);
    return (zoneRows != nullptr) ? zoneRows[zone] : -1;
}

namespace {
Rect ReadRect(const uint8_t *rec) {
    Rect r;
    r.left = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_LEFT);
    r.right = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_RIGHT);
    r.top = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_TOP);
    r.bottom = Game::Read<float>(rec, Offsets::OFF_WMA_LOC_BOTTOM);
    return r;
}

// Zone world-map canvas size (WorldMapDetailFrame); WorldMapOverlay hit
// rects live in this pixel space. Shared with the map-art readers.
constexpr double kZoneCanvasW = kMapCanvasWidth;
constexpr double kZoneCanvasH = kMapCanvasHeight;

// How deep inside its containing WorldMapOverlay hit rect the map-percent point
// (0..100) sits on WMA row `wmaRow` — the max, over that zone's overlays whose
// hit rect encloses the point, of the normalized margin to the nearest overlay
// edge (0 at the edge .. 0.5 at center). Returns -1 when the point is on none
// of the zone's overlays.
//
// This is the runtime analog of an ADT area-grid lookup. WorldMapArea rects are
// loose overlapping boxes; the overlay hit rects trace the real landmass, so
// "which zone is the point drawn on" disambiguates them (Un'Goro's crater vs
// Thousand Needles' empty overlap). But hit rects are themselves rectangles
// that overlap at borders, so the DEPTH matters: a border node deep inside its
// own zone's overlay beats one merely clipping a neighbor's overlay edge
// (Nijel's Point is deep in Desolace's "Nijel's Point" overlay but only nicks
// the edge of Stonetalon's Charred Vale box).
double OverlayMargin(int wmaRow, double mapXpct, double mapYpct) {
    const double cx = mapXpct / 100.0 * kZoneCanvasW;
    const double cy = mapYpct / 100.0 * kZoneCanvasH;
    const int count =
        Game::Read<int>(Offsets::VAR_WORLDMAP_OVERLAY_COUNT);
    double best = -1.0;
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_OVERLAY_RECORDS,
                                         Offsets::VAR_WORLDMAP_OVERLAY_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr ||
            Game::Read<int>(rec, Offsets::OFF_WMO_WORLDMAP_AREA) != wmaRow)
            continue;
        const int hl = Game::Read<int>(rec, Offsets::OFF_WMO_HITRECT_LEFT);
        const int hr = Game::Read<int>(rec, Offsets::OFF_WMO_HITRECT_RIGHT);
        const int ht = Game::Read<int>(rec, Offsets::OFF_WMO_HITRECT_TOP);
        const int hb = Game::Read<int>(rec, Offsets::OFF_WMO_HITRECT_BOTTOM);
        const double sw = hr - hl, sh = hb - ht;
        if (sw <= 0.0 || sh <= 0.0)
            continue; // no / degenerate hit rect
        if (cx < hl || cx > hr || cy < ht || cy > hb)
            continue; // point outside this overlay
        const double mX = (cx - hl < hr - cx ? cx - hl : hr - cx) / sw;
        const double mY = (cy - ht < hb - cy ? cy - ht : hb - cy) / sh;
        const double m = mX < mY ? mX : mY;
        if (m > best)
            best = m;
    }
    return best;
}
} // namespace

bool ZonePercent(int mapID, float x, float y, int *outAreaID, double *outMapX,
                 double *outMapY) {
    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_AREA_COUNT);
    // Two candidate tracks over the zones whose rect contains the point:
    //   land* — the point is on the zone's drawn landmass (an overlay hit rect
    //           encloses it): authoritative, wins whenever any zone qualifies.
    //           Scored by how DEEP inside the overlay the point sits, so a
    //           border node deep in its own zone's overlay beats one nicking a
    //           neighbor's overlay edge.
    //   box*  — bounding-box only: the fallback when the point is on no zone's
    //           overlay (map gaps / overlay-less custom zones), scored by the
    //           most-interior zone-rect margin.
    bool haveLand = false, haveBox = false;
    double landScore = -1.0, boxMargin = 0.0;
    int landArea = 0, boxArea = 0;
    double landX = 0.0, landY = 0.0, boxX = 0.0, boxY = 0.0;
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                         Offsets::VAR_WORLDMAP_AREA_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        if (Game::Read<int>(rec, Offsets::OFF_WMA_MAP_ID) != mapID)
            continue;
        const int areaID = Game::Read<int>(rec, Offsets::OFF_WMA_AREA_ID);
        if (areaID == 0)
            continue; // continent-spanning row, not a zone

        const Rect r = ReadRect(rec);
        if (!r.Valid())
            continue; // degenerate / unmapped zone
        if (!r.Contains(x, y))
            continue; // point outside this zone's rect

        // mapX = horizontal (world Y), mapY = vertical (world X).
        const double mapX = r.Px(y) * 100.0;
        const double mapY = r.Py(x) * 100.0;
        const double marginX = (x - r.bottom < r.top - x ? x - r.bottom : r.top - x) / (r.top - r.bottom);
        const double marginY = (y - r.right < r.left - y ? y - r.right : r.left - y) / (r.left - r.right);
        const double margin = marginX < marginY ? marginX : marginY;

        if (!haveBox || margin > boxMargin) {
            haveBox = true;
            boxMargin = margin;
            boxArea = areaID;
            boxX = mapX;
            boxY = mapY;
        }
        const double overlayMargin = OverlayMargin(id, mapX, mapY);
        if (overlayMargin >= 0.0 && (!haveLand || overlayMargin > landScore)) {
            haveLand = true;
            landScore = overlayMargin;
            landArea = areaID;
            landX = mapX;
            landY = mapY;
        }
    }

    if (haveLand) {
        *outAreaID = landArea;
        *outMapX = landX;
        *outMapY = landY;
        return true;
    }
    if (haveBox) {
        *outAreaID = boxArea;
        *outMapX = boxX;
        *outMapY = boxY;
        return true;
    }
    return false;
}

bool PercentInZone(int areaID, float x, float y, double *outMapX, double *outMapY) {
    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_AREA_COUNT);
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                         Offsets::VAR_WORLDMAP_AREA_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        if (Game::Read<int>(rec, Offsets::OFF_WMA_AREA_ID) != areaID)
            continue;
        const Rect r = ReadRect(rec);
        if (!r.Valid())
            continue; // degenerate row — keep scanning for a usable one
        if (!r.Contains(x, y))
            return false; // point outside this zone's rect — not in this zone
        *outMapX = r.Px(y) * 100.0; // horizontal (world Y)
        *outMapY = r.Py(x) * 100.0; // vertical (world X)
        return true;
    }
    return false;
}

int RowForUiMapID(int uiMapID) {
    if (uiMapID > 0)
        return RowForAreaID(static_cast<uint32_t>(uiMapID));
    if (uiMapID < 0) {
        const int row = -uiMapID;
        if (DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                        Offsets::VAR_WORLDMAP_AREA_COUNT,
                        static_cast<uint32_t>(row)) != nullptr)
            return row;
    }
    return -1;
}

int UiMapIDForRow(int row) {
    if (row <= 0)
        return 0;
    const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                     Offsets::VAR_WORLDMAP_AREA_COUNT,
                                     static_cast<uint32_t>(row));
    if (rec == nullptr)
        return 0;
    const int areaID = Game::Read<int>(rec, Offsets::OFF_WMA_AREA_ID);
    return (areaID != 0) ? areaID : -row;
}

int ContinentRowForMapID(int mapID) {
    const int count = Game::Read<int>(Offsets::VAR_WORLDMAP_AREA_COUNT);
    for (int id = 1; id <= count; ++id) {
        const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                         Offsets::VAR_WORLDMAP_AREA_COUNT,
                                         static_cast<uint32_t>(id));
        if (rec == nullptr)
            continue;
        if (Game::Read<int>(rec, Offsets::OFF_WMA_MAP_ID) != mapID)
            continue;
        if (Game::Read<int>(rec, Offsets::OFF_WMA_AREA_ID) != 0)
            continue; // a zone row, not the continent-spanning one
        if (!ReadRect(rec).Valid())
            continue; // stray zero-rect continent row (the "World" row)
        return id;
    }
    return -1;
}

bool RowRect(int row, Rect *outRect, int *outMapID) {
    if (row <= 0)
        return false;
    const uint8_t *rec = DBC::Record(Offsets::VAR_WORLDMAP_AREA_RECORDS,
                                     Offsets::VAR_WORLDMAP_AREA_COUNT,
                                     static_cast<uint32_t>(row));
    if (rec == nullptr)
        return false;
    if (outRect != nullptr)
        *outRect = ReadRect(rec);
    if (outMapID != nullptr)
        *outMapID = Game::Read<int>(rec, Offsets::OFF_WMA_MAP_ID);
    return true;
}

bool PercentInRow(int row, float x, float y, double *outPx, double *outPy) {
    Rect r;
    if (!RowRect(row, &r, nullptr) || !r.Valid())
        return false;
    *outPx = r.Px(y);
    *outPy = r.Py(x);
    return true;
}

bool WorldFromRow(int row, double px, double py, double *outX, double *outY) {
    Rect r;
    if (!RowRect(row, &r, nullptr) || !r.Valid())
        return false;
    *outY = r.WorldY(px);
    *outX = r.WorldX(py);
    return true;
}

bool ContinentPercent(int mapID, float x, float y, double *outPx, double *outPy) {
    const int row = ContinentRowForMapID(mapID);
    return row > 0 && PercentInRow(row, x, y, outPx, outPy);
}

} // namespace Map::Area
