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

#include <cstddef>
#include <cstdint>

namespace Item::Icon {

// Writes `Interface\Icons\<name>` for an `ItemDisplayInfo.dbc` row into
// `out`. The one place this path is built — `GetItemInfo`,
// `GetItemInfoInstant`, `C_Item.GetItemData` and the container getters all
// come through here, so a change to the chain has a single site. Returns
// false (out empty) when the row is missing or carries no icon.
bool PathForDisplayInfoID(uint32_t displayInfoID, char *out, size_t outSize);

// Same, starting from an itemID: the cached item's ItemStats
// `displayInfoID`, then the row above. Returns false when the item isn't
// cached. Passive: never warms the cache.
bool PathForItemID(uint32_t itemID, char *out, size_t outSize);

} // namespace Item::Icon
