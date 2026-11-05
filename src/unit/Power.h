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

namespace Unit::Power {

// Modern power-type token for a PowerType value: 0 MANA, 1 RAGE, 2 FOCUS,
// 3 ENERGY, 4 HAPPINESS, 5 RUNES, 6 RUNIC_POWER, anything else "UNKNOWN".
// Used by `UnitPowerType`'s second return and by spell power-cost APIs.
const char *PowerTypeToken(int type);

// The engine's divisor from a stored power amount to displayed units
// (`FUN_POWER_DISPLAY_DIVISOR`): 10 for rage, 1000 for happiness, 1 otherwise.
// Every power amount surfaced to Lua — unit power, spell costs, combat-log
// amounts — goes through this.
unsigned int DisplayDivisor(int type);

} // namespace Unit::Power
