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

// Map.dbc's instance type for a map: what kind of place it is.
namespace Map::Instance {

// Map.dbc InstanceType (OFF_MAP_INSTANCE_TYPE): 0 open world, 1 dungeon,
// 2 raid, 3 battleground. -1 for a map with no row.
int Type(int mapID);

// Type() of the map the player is on (VAR_CURRENT_MAP_ID).
int CurrentType();

} // namespace Map::Instance
