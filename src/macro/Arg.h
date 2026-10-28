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

namespace Macro::Arg {

// The macro slot (1-based) that argument 1 names, as 4.3.4's GetMacroFromLua
// reads it: a number, or a string that converts to one, is an index; any other
// string is a name. 0 when no macro is there.
int Slot(void *L);

} // namespace Macro::Arg
