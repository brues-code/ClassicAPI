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

namespace Slider::Value {

// The later clients' paging press, for an enabled slider's mouse-down: with
// stepsPerPage 1 or more and a thumb, a press outside the thumb moves the
// value stepsPerPage steps toward the cursor and returns true; the caller then
// runs only the base frame's mouse-down, so no drag starts. False otherwise:
// the press is 1.12's (drag from the cursor).
bool Page(void *slider, const void *event);

} // namespace Slider::Value
