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

namespace Macro::Execute {

// Runs `text` as a macro body through the engine's macro runner, as 4.3.4's
// RunMacroText does. `button` is the running macro's button; it may be null.
void Text(const char *text, const char *button);

// Runs a saved macro, named or given as a 1-based index in decimal, through
// the same runner. Returns false when no macro matches.
bool Saved(const char *macro, const char *button);

} // namespace Macro::Execute
