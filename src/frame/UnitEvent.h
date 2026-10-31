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

namespace Frame::UnitEvent {

// The RegisterUnitEvent filter for one fire whose first argument is the
// string `arg1`: false when `frame` registered `eventName` (the event-table
// entry's own name) with units and `arg1` matches none of them. True for a
// plain registration. The engine's dispatches meet this filter at the runner;
// a dispatcher of our own that walks the subscriber chain itself
// (Event::Custom::FireValues) asks here for each frame.
bool Accepts(const void *frame, const char *eventName, const char *arg1);

} // namespace Frame::UnitEvent
