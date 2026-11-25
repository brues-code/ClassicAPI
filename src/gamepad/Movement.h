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

// The sticks moving the character and the camera, and gamepad freelook, as
// the 3.80.2 client drives them (0x141264B20).
namespace GamePad::Movement {

// One frame, after the buttons and sticks went to the frames.
void Update();

// Releases the movement the gamepad holds and leaves freelook, as when
// gamepad support stops.
void Reset();

} // namespace GamePad::Movement
