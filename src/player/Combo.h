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

// The player's combo points and the unit they sit on, read as
// Script_GetComboPoints (0x0051A190) reads them: the CGPlayer +0xE68 sub-struct
// (OFF_CGPLAYER_INFO). 0 before the player object exists, so either is safe
// from any context (a send observer, a tick).

#include "Offsets.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Player::Combo {

namespace Detail {
inline const uint8_t *Info() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    return player != nullptr ? *reinterpret_cast<const uint8_t *const *>(player + Offsets::OFF_CGPLAYER_INFO)
                             : nullptr;
}
} // namespace Detail

inline uint8_t Points() {
    const uint8_t *info = Detail::Info();
    return info != nullptr ? info[Offsets::OFF_CGPLAYER_COMBO_POINTS] : 0;
}

inline uint64_t Target() {
    const uint8_t *info = Detail::Info();
    return info != nullptr ? *reinterpret_cast<const uint64_t *>(info + Offsets::OFF_CGPLAYER_COMBO_TARGET) : 0;
}

} // namespace Player::Combo
