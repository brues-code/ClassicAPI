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

// Runs raw macro text through vanilla FrameXML's ChatEdit_ParseText dispatcher.
void Text(void *L, const char *text);

// Runs a saved macro by name or decimal index. Prefers an addon-provided
// RunMacro implementation when present, then falls back to GetMacroInfo plus
// the stock text dispatcher.
bool Saved(void *L, const char *macro);

} // namespace Macro::Execute
