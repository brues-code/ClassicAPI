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

// The active player's combo points, which 1.12 keeps on the player (byte 1 of
// PLAYER_FIELD_BYTES, the combo target's GUID in PLAYER_FIELD_COMBO_TARGET)
// and the 3.80.2 client exposes as power type 4.
namespace Target::ComboPoints {

// The server caps the count at 5 (Player::AddComboPoints); 1.12 has no field
// for the maximum.
constexpr uint32_t kMax = 5;

// Whether the class uses combo points: rogue and druid, the classes 1.12's
// GetComboPoints answers for. Warriors also get the byte (Overpower's window)
// and are left out, as both clients leave them out.
bool ClassUses(const uint8_t *playerFields);

// The active player's combo-point byte, whatever the target; 0 without a
// player in the world.
uint32_t Count();

} // namespace Target::ComboPoints
