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

namespace Container::Search {

// 4.3.4's `CGContainerInfo::IsItemFiltered`: true when a bag search is
// active and the item matches none of its name, its equipment sets' names,
// its subclass name or its equip slot's name. False with no search, and for
// an item whose static data isn't cached yet.
bool IsItemFiltered(const uint8_t *cgItem);

} // namespace Container::Search
