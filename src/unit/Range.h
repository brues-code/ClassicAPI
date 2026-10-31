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

namespace Unit::Range {

// UnitInRange's check for `token`: true (the range was checked) with
// `*inRange` set when both positions are known; false for an absent unit,
// no active mover, and "player". `token` must be a unit token — the engine's
// resolver raises a Lua error for any other string.
bool InRange(const char *token, bool *inRange);

} // namespace Unit::Range
