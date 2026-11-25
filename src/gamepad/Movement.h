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

// The sticks moving the character and the camera, and gamepad freelook, as
// the 3.80.2 client drives them (0x141264B20).
namespace GamePad::Movement {

// One frame, after the buttons and sticks went to the frames.
void Update();

// Releases the movement the gamepad holds and leaves freelook, as when
// gamepad support stops.
void Reset();

// Gamepad freelook (3.80.2's input-control bit 0x2000000) and its toggle
// 0x1412648B0, which fires GAME_PAD_ACTIVE_CHANGED on a change.
bool FreelookOn();
void SetFreelook(bool on);

// True when real mouse motion came within GamePadOverlapMouseMs.
bool MouseRecent(uint32_t now);

// The world's camera and the unit the player moves, or null.
void *ActiveCamera();
void *ActiveMover();

// CGCamera_ApplyMouseLook (0x141B621A0) in its own units, then the character
// turns with the camera when GamePadTurnWithCamera says so.
void MouseLook(float yaw, float pitch);

} // namespace GamePad::Movement
