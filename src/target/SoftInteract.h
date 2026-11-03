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

namespace Target::SoftInteract {

// The `softinteract` GUID: the object the interact key would use, or 0.
// Read by the token resolver in `unit/TokenExtensions.cpp`.
uint64_t Get();

// The `anyinteract` GUID: the current target when the player can interact
// with it right now (in reach; on screen or not), else `softinteract`, else
// the target when it is interactable but out of reach, else 0.
uint64_t GetAny();

// True when the player can interact with `object` right now: the hover
// cursor shows its interact mode, not the Unable form.
bool InReach(void *object);

} // namespace Target::SoftInteract
