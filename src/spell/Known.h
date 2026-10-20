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

namespace Spell::Known {

// True iff the local player knows `spellID`, per the engine's known-spell
// bitmap at `[VAR_PLAYER_SPELL_BITMAP]` (the bit the helper at 0x0060C740
// reads): trained abilities, talents and talent passives, racials,
// profession recipes. Rebuilt for each character at login. False for IDs
// out of range or before the bitmap exists.
bool IsKnown(uint32_t spellID);

// True iff the local player can hold a weapon in the off hand.
bool CanDualWield();

} // namespace Spell::Known
