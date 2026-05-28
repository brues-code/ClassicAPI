// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>

namespace Unit::Focus {

// Current focus GUID, or `0` when nothing is focused. Read by the
// `focus` / `focustarget*` branch of the token resolver in
// `unit/TokenExtensions.cpp`.
uint64_t Get();

// Sets focus to `guid` and fires `PLAYER_FOCUS_CHANGED` (no args).
// Identity-checks first — assigning the same GUID is a no-op,
// matching 3.3.5's `FUN_0051FF20` behavior so the event only fires
// on a real transition. Pass `0` to clear focus.
void Set(uint64_t guid);

} // namespace Unit::Focus
