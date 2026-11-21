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

namespace Container::SlotFlags {

// Enum.BagSlotFlags bits.
constexpr uint32_t DISABLE_AUTO_SORT = 0x1;

// The BagSlotFlags word of the bag slot that holds Lua container `bagID`:
// equipped bags 1..4 and bank bags 5..10. 0 for every other ID (the
// backpack and the main bank have their own autosort settings instead).
uint32_t Of(int bagID);

} // namespace Container::SlotFlags
