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

#include "target/Reason.h"

#include <cstdint>

namespace Target::Tab {

// Target::Reason's setter, after a new target took (`old` is the one it
// replaced, or 0): the cycle's snapshot of when and where, and a click's
// history seed.
void OnSet(uint64_t old, uint64_t guid, Reason::Why why);

// The target was cleared: the cycle history is forgotten.
void OnClear();

} // namespace Target::Tab
