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

namespace Unit::PowerEvents {

// UNIT_POWER_UPDATE then UNIT_POWER_FREQUENT, `(token, powerType)`, once per
// token of `guid`, for a change of its 1.12 power `slot` (the payload carries
// the slot's modern token). For a power change the type-wide watchers here
// can't see, such as a group member's party record.
void SignalPower(uint64_t guid, int slot);

// UNIT_MAXPOWER, `(token, powerType)`, once per token of `guid`, for `slot`.
void SignalMaxPower(uint64_t guid, int slot);

// UNIT_POWER_UPDATE then UNIT_POWER_FREQUENT with "COMBO_POINTS", once per
// token of the player `guid`: a change of its combo-point byte.
void SignalComboPoints(uint64_t guid);

} // namespace Unit::PowerEvents
