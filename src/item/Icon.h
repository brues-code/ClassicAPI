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

// Writes `Interface\Icons\<name>` for the cached item into `out` — the
// ItemStats `displayInfoID` → `ItemDisplayInfo.dbc` icon field, the same
// chain `GetItemInfo` / `GetItemIcon` use. Returns false (out empty) when
// the item isn't cached or the display row has no icon. Passive: never
// warms the cache.
bool PathForItemID(uint32_t itemID, char *out, size_t outSize);

} // namespace Item::Icon
