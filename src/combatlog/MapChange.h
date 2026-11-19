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

// The combat log file's MAP_CHANGE line and the world map it names.

namespace CombatLog::MapChange {

// The uiMapID of the world map the player is on, 0 for none. The advanced
// block's uiMapID slot.
int UiMapID();

// Names the player's map now, as a header does: a MAP_CHANGE when there is
// one, written even when the file named the same map last.
void Announce();

} // namespace CombatLog::MapChange
