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

namespace Target::GamepadInteract {

// The unit the gamepad bar's interact button acts on, as the last
// SetPreferredGamepadInteractTarget named it; 0 for none.
uint64_t Preferred();

// The gamepad icon for `guid`: the left face button glyph in the active
// controller's label style when `guid` is the preferred unit, else null.
const char *GlyphFor(uint64_t guid);

} // namespace Target::GamepadInteract
