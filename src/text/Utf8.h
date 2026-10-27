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

// The two Storm UTF-8 string helpers that 1.12's Storm does not have
// (its only UTF-8 routine is `SUniConvertUTF8to16`), mirrored from 4.3.4's
// `SStr.cpp` so the Lua functions built on them answer exactly as the newer
// clients do. Both stop at the first NUL, as the originals do.
namespace Text::Utf8 {

// `SStrLenUTF8(string)`: the number of code points — every byte that is not
// a continuation byte (`10xxxxxx`) starts one.
size_t Length(const char *s);

// `SStrCmpUTF8I(string1, string2, STORM_MAX_STR)`: case-insensitive compare.
// Decodes one code point at a time with 4.3.4's `SStrCmpGetNextTextUpper`
// fold (ASCII and Latin-1 letters, œ, and the Cyrillic alphabet; every other
// code point compares as itself) and returns the first difference of the
// folded values: negative when `a` sorts first, 0 when equal, positive when
// `b` sorts first. Only the sign carries meaning.
int CompareI(const char *a, const char *b);

} // namespace Text::Utf8
