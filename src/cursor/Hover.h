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

namespace Cursor::Hover {

// The cursor mode (Offsets::CURSOR_MODE_*) the world cursor would show with
// the mouse over `object` — a unit, player, game object or corpse — or 0
// when it shows none. Computes it without showing it.
uint32_t Mode(void *object);

// For a game object's named cursor (CURSOR_MODE_CUSTOM): whether the player
// may use the object now.
bool IsUsable(void *object);

} // namespace Cursor::Hover
