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
#include <string>

// Launch-switch parsing, shared by every module that reads one.
//
// A switch is written `-name value` or `-name=value`, and the `/name` form
// Windows tools also accept is honored. The value may be double-quoted so it can
// contain spaces, which a Lua snippet passed on the command line always will.
// The name is matched case-insensitively and only at a token boundary, so a path
// that happens to contain it cannot be mistaken for the switch itself.
//
// One parser for all of them on purpose: a second copy would drift, and the
// quoting and boundary rules are exactly where that drift would go unnoticed.

namespace Config::CommandLine {

// Copies the value of `-name` into `out`. False when the switch is absent, its
// value is empty, or the value does not fit -- a value too long to hold is a
// failure rather than a silent truncation, since a half-copied filename or
// snippet would be acted on as though it were complete.
bool Value(const char *name, char *out, size_t outSize);

// Same, bounded only by the command line itself. For values with no natural
// length, such as a script body.
bool Value(const char *name, std::string &out);

} // namespace Config::CommandLine
