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

#include "cvar/Factory.h"
#include "gamepad/Movement.h"

namespace Target::SoftTarget {

// A SoftTarget<Kind> cvar's flags (Enum.SoftTargetEnableFlags): 1 gamepad,
// 2 keyboard and mouse, 3 both. As 3.80.2's soft pick (FUN_1405e1410) reads
// them: "gamepad" is the freelook bit 0x2000000 of the input-control flags
// (DAT_1444a7a54, toggled by 0x1412648B0), so bit 1 counts while the pad
// drives the camera and bit 2 the rest of the time.
inline bool Enabled(CVar::Factory::Handle enable) {
    const int v = CVar::Factory::GetInt(enable);
    return (v & (GamePad::Movement::FreelookOn() ? 1 : 2)) != 0;
}

} // namespace Target::SoftTarget
