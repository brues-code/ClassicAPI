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

namespace Map::Overlays {

// Which overlays of a zone to include, by the local player's exploration.
enum class Filter {
    All,        // every WorldMapOverlay.dbc row for the zone (C_Map.GetMapOverlays)
    Explored,   // only overlays the player has discovered
    Unexplored, // only overlays the player has NOT discovered
};

// Pushes an array of overlay tables for `wmaRow` (a WorldMapArea.dbc row),
// filtered by exploration, onto the Lua stack as a single value. Shared by
// `C_Map.GetMapOverlays` (Filter::All) and `C_MapExplorationInfo`'s
// explored / unexplored texture getters. The caller resolves the row (via
// `Map::Area`) and clears the stack first; this leaves exactly one table.
// The per-overlay table shape is documented in Overlays.cpp's header.
//
// Explored/Unexplored read the player's explored-areas bitfield; before the
// player is resident (character select) every overlay reads as unexplored.
void PushZoneOverlays(void *L, int wmaRow, Filter filter);

// Whether the local player has explored AreaTable `areaID`, by the engine's
// own test (an area whose ExplorationLevel is < 0 always counts). `player`
// is `Unit::Identity::PlayerObject()`; null counts nothing as explored
// except those always-explored areas.
bool IsAreaExplored(int areaID, const uint8_t *player);

// Whether the local player has explored a WorldMapOverlay row: any of its
// four areas explored, as the engine's map rebuild decides. False with no
// player.
bool IsOverlayExplored(const uint8_t *overlayRec, const uint8_t *player);

// The alpha (0..255) of a WorldMapOverlay row's art at a point on its zone
// canvas (kMapCanvasWidth x kMapCanvasHeight px), placed exactly as
// `GetMapOverlays` tiles draw it. 0 off the art and for a row with no art.
// The art is decoded through the engine's BLP reader on the first query
// that lands on it and cached for the session.
int ArtAlphaAt(const uint8_t *overlayRec, double canvasX, double canvasY);

} // namespace Map::Overlays
