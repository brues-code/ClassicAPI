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

namespace Target::SoftTarget {

// A SoftTarget<Kind> cvar's 0 off / 1 gamepad / 2 KBM / 3 always. 1.12 has
// no gamepad, so only 2 and 3 turn the kind on.
inline bool Enabled(CVar::Factory::Handle enable) {
    const int v = CVar::Factory::GetInt(enable);
    return v == 2 || v == 3;
}

} // namespace Target::SoftTarget
