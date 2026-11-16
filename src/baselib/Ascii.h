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

// Tiny ASCII string helpers. Header-only — no locale, no allocation, safe from
// any context (hook callbacks, DBC walks). Use these instead of hand-rolling
// yet another case-insensitive compare.

#include <cstddef>

namespace Ascii {

// Fold a single ASCII char to lower case (non-letters unchanged).
inline char ToLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive full-string equality over ASCII. Two null pointers compare
// equal; a null vs non-null does not.
inline bool EqualCI(const char *a, const char *b) {
    if (a == nullptr || b == nullptr)
        return a == b;
    for (; *a && *b; ++a, ++b)
        if (ToLower(*a) != ToLower(*b))
            return false;
    return *a == *b;
}

// Case-insensitive equality of the `n` bytes at `s` (not terminated) against
// the terminated `lit`: true only when `lit` is exactly `n` long.
inline bool EqualCI(const char *s, size_t n, const char *lit) {
    for (size_t i = 0; i < n; ++i)
        if (lit[i] == '\0' || ToLower(s[i]) != ToLower(lit[i]))
            return false;
    return lit[n] == '\0';
}

// Case-insensitive "`s` starts with `prefix`"; stops at `s`'s terminator, so a
// short `s` is never read past.
inline bool StartsWithCI(const char *s, const char *prefix) {
    for (size_t i = 0; prefix[i] != '\0'; ++i)
        if (s[i] == '\0' || ToLower(s[i]) != ToLower(prefix[i]))
            return false;
    return true;
}

} // namespace Ascii
