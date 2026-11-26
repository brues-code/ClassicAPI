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

// The item behind an action slot (both ranges), 4.3.4's CGActionBar::GetItem.
namespace Action::SlotItem {

// An item word's item, or the item a macro's `#showtooltip` / `#show`
// resolved to; 0 otherwise.
uint32_t ItemForSlot(int slot0);

// The item's max stack is more than 1 (4.3.4's IsStackableAucItem); false
// while it is not cached.
bool IsStackable(int slot0);

} // namespace Action::SlotItem
