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

namespace Bindings::Command {

// True for the direct-action command families a key binding can hold:
// "SPELL x", "ITEM x", "MACRO x", "CLICK frame:button" (case-insensitive
// prefix, as 4.3.4 tests them).
bool IsDirectAction(const char *command);

// Runs a direct-action command for a key press or release, as 4.3.4's
// CGUIBindings::ExecKey does. Returns the engine's handled flag: 1 for SPELL,
// ITEM and MACRO, 1 for CLICK when the frame exists, else 0. Only call it for
// IsDirectAction commands.
int RunKey(const char *command, int isDown);

} // namespace Bindings::Command
