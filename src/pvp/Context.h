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

// 3.80.2's two PvP tests on 1.12's fields. Soft targeting's priority
// (target/Priority.cpp) and the camera follow's locked target
// (camera/AutoFollow.cpp) both read them.
namespace Pvp::Context {

// FUN_1405e35d0: the player is in a PvP context: dueling, free-for-all
// PvP, or on a battleground map. (3.80.2 also counts an arena and its "in
// PvP combat" flag; 1.12 has neither.)
bool PvpContext(const void *player);

// FUN_1405e3530: the unit is a PvP unit. A creature: player-controlled and
// PvP flagged. A player: PvP flagged, free-for-all, wanting PvP
// (PLAYER_FLAGS_PVP_DESIRED), or in a duel with the player.
bool PvpUnit(void *player, void *unit);

} // namespace Pvp::Context
