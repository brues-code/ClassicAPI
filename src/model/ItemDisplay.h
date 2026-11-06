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

namespace Model::ItemDisplay {

// AttachDisplayModel's item step: when `record` is the item record SetItem
// gave `pane`, builds the item's model in the pane and returns true. Any
// other record returns false and is the engine's to build.
bool Attach(void *pane, const uint8_t *record);

// The ItemVisuals row an ItemDisplayInfo `display` hangs itself, or 0 when it
// names none that exists. An enchant's glow shows only when this is 0
// (GetItemDescVisual 0x0062EC70).
uint32_t OwnVisual(const uint8_t *display);

} // namespace Model::ItemDisplay
