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

namespace Macro::RunBody {

// Runs `entry`'s body through the engine's runner (FUN_MACRO_RUN_BODY) with
// `button` as the running macro's button: what GetRunningMacroButton() returns
// and [button] tests while the body runs. `button` may be null.
void Run(const uint8_t *entry, const char *button);

} // namespace Macro::RunBody
