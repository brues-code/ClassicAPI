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

namespace Frame::Cooldown {

// Marks a frame object (a Model built by the CreateFrame hook) as a
// Cooldown, giving it 4.3.4's Cooldown methods and type name. Called by
// `Frame::CreateFrame` once the engine has built the frame.
void Mark(void *frameObject);

// True for a frame object passed to `Mark` in this UI session.
bool IsMarked(void *frameObject);

} // namespace Frame::Cooldown
