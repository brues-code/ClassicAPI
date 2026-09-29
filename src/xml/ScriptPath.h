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

#include <cstddef>
#include <cstdint>

namespace Xml::ScriptPath {

// Path to load for a `FUN_LUA_LOAD_FILE` call. For the XML `<Script file>`
// call site only (`retAddr == RET_XML_SCRIPT_FILE_LOAD`), a `file=` value that
// contains a subdirectory and does not exist as given is retried relative to
// the XML file being processed, as later clients resolve it; the joined path
// is written to `buf` and returned when that file exists. Every other call,
// and every path that already resolves, is returned unchanged.
const char *Resolve(const char *path, uintptr_t retAddr, char *buf, size_t bufSize);

} // namespace Xml::ScriptPath
