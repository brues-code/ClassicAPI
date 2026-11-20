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

#include "Offsets.h"
#include "unit/Flags.h"

#include <cstdint>

namespace NamePlate::Side {

// Whether `unit` takes a friendly plate for `player` (the active player): the
// plate gate's own test (0x0060F6B7..0x0060F6F1). A player is friendly when
// the two can cooperate and the player cannot attack it; any other unit when
// the player cannot attack it. The gate then asks ShowFriendNameplates' bit
// of a friendly unit and ShowNameplates' of any other.
inline bool IsFriendly(const void *player, const void *unit) {
    using Test_t = bool(__thiscall *)(const void *self, const void *other);
    const auto canAttack = reinterpret_cast<Test_t>(Offsets::FUN_UNIT_CAN_ATTACK);
    if (Unit::Flags::IsPlayerObject(static_cast<const uint8_t *>(unit)))
        return reinterpret_cast<Test_t>(Offsets::FUN_UNIT_CAN_COOPERATE)(player, unit) &&
               !canAttack(player, unit);
    return !canAttack(player, unit);
}

} // namespace NamePlate::Side
