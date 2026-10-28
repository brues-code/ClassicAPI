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

namespace Item::Dressable {

// The item a recipe makes, as 4.3.4's dressing room reads it: for the first
// of the cached `record`'s five spells whose effect 0 is LEARN_SPELL and
// whose taught spell's effect 0 is CREATE_ITEM, that effect's item. 0 when
// no spell qualifies.
uint32_t RecipeItem(const uint8_t *record);

} // namespace Item::Dressable
