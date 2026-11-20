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

namespace CombatLog::DeathRecap {

// The recap of the local player's death, made from the kept combat log before
// the death's UNIT_DIED entry is queued (3.80.2's FUN_1414407d0). Every call
// takes the next recap ID. Returns that ID, or -1 when the log holds no hit
// to recap, which 3.80.2 then keeps no recap for.
int OnPlayerDied(uint64_t player);

} // namespace CombatLog::DeathRecap
