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

namespace Target::SoftCast {

// The unit a cast of `record` with no unit given goes to in place of the
// selection — the side's any-unit (3.80.2's default spell target,
// FUN_1413d1ac0) — or 0 when it goes to the selection as 1.12 has it.
uint64_t DefaultTarget(const uint8_t *record);

} // namespace Target::SoftCast
