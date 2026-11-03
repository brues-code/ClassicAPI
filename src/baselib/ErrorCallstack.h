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

namespace BaseLib::ErrorCallstack {

// The innermost call level whose function is the engine's error function
// (FUN_FRAMESCRIPT_CALL_ERROR_HANDLER), or 0 when no error is being
// reported. Nonzero exactly while an error handler runs.
int ErrorFunctionLevel(void *L);

} // namespace BaseLib::ErrorCallstack
