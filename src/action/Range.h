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

#include <cstdint>

namespace Action::Range {

// IsActionInRange's answer for a slot (0-based, the engine's 0..119 and
// Action::Extended's 120..359): 0 when it has none (nil), else 1 with
// `*inRange` 0 or 1. `unit` is the GUID to check against; 0 takes the
// macro's unit, then the soft default target, then the selection.
int Answer(int slot0, uint64_t unit, int *inRange);

} // namespace Action::Range
