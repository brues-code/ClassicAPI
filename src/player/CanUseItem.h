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

namespace Player::CanUseItem {

// The engine's `CGPlayer_C::CanUseItem` (FUN_PLAYER_CAN_USE_ITEM) for an
// item-stats cache record: true iff `player` meets the record's level,
// class, race, proficiency, skill, spell, rank and reputation needs.
// Both pointers must be non-null.
bool Check(const uint8_t *player, const uint8_t *record);

} // namespace Player::CanUseItem
