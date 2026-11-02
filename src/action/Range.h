// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

namespace Action::Range {

// IsActionInRange's answer for a slot (0-based, 0..119): 0 when it has none
// (IsActionInRange's nil), else 1 with `*inRange` 0 or 1. A macro aimed at a
// unit is checked against that unit.
int Answer(int slot0, int *inRange);

} // namespace Action::Range
