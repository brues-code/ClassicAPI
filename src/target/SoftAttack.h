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

namespace Target::SoftAttack {

// FUN_ATTACK_RESOLVE_TARGET with action targeting: with no GUID given and no
// target, the melee attack settles on `softenemy`, and the target stays
// empty. Otherwise the engine's resolve unchanged. Nonzero when `guid` holds
// a unit to attack; else the engine has shown why not.
int ResolveMelee(void *player, uint64_t *guid);

} // namespace Target::SoftAttack
