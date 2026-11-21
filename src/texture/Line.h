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

// The Line region type (frame:CreateLine, XML <Line>) — see texture/Line.cpp.

namespace Texture::Line {

// The region is a Line. Null-safe.
bool Is(const void *region);

// Builds a <Line> element's line from the texture region the layer loader
// just built for it: the Line vtables and endpoints, then the node's and its
// templates' `thickness`, <StartAnchor> and <EndAnchor>, then the default
// endpoints.
void LoadFromXml(void *region, const void *node, void *status);

// Writes the line's quad again, after something it depends on changed
// without a layout (the thickness, a vertex offset).
void Rebuild(void *region);

// The lengths a tiled line repeats its image over, in UI units: the line's
// length on the screen across, its thickness down. False when the region is
// not a line.
bool TileSize(const void *region, float &width, float &height);

} // namespace Texture::Line
