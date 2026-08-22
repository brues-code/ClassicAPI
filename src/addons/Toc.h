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
#include <cstring>

namespace AddOns::Toc {

// Fold an ASCII letter to lower case (leaves every other byte unchanged).
inline char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Case-insensitive equality of the `n` bytes at `s` against the NUL-terminated
// `lit` (ASCII). True only when the lengths match too. Shared by the addon
// modules that scan `…\AddOns\<Name>\…` paths and TOC directive lines.
inline bool EqCI(const char *s, size_t n, const char *lit) {
    for (size_t i = 0; i < n; ++i)
        if (lit[i] == '\0' || Lower(s[i]) != Lower(lit[i]))
            return false;
    return lit[n] == '\0';
}

// Scans an in-memory TOC buffer for `directive` (the full line prefix, e.g.
// "## Version:") at a line start, case-insensitively, and returns the value
// that follows via out-params: [*valStart, *valStart + *valLen), trimmed of
// leading and trailing spaces/tabs and excluding the CR/LF/EOF terminator.
// Returns false (out-params untouched) when the directive is absent. Pure —
// reads no files; the caller owns the buffer (via FUN_FILE_READ, or an embedded
// copy). Shared by addons/LocalTable.cpp (AllowAddOnTableAccess) and
// addons/Embedded.cpp (Version) so the line-scan lives in one place.
inline bool FindValue(const char *buf, size_t size, const char *directive,
                      const char **valStart, size_t *valLen) {
    const size_t dlen = std::strlen(directive);
    if (dlen == 0)
        return false;
    for (size_t i = 0; i + dlen <= size; ++i) {
        const bool atLineStart = (i == 0) || buf[i - 1] == '\n';
        if (!atLineStart || _strnicmp(buf + i, directive, dlen) != 0)
            continue;
        const char *p = buf + i + dlen;
        const char *end = buf + size;
        while (p < end && (*p == ' ' || *p == '\t'))
            ++p;
        const char *v = p;
        while (p < end && *p != '\r' && *p != '\n')
            ++p;
        while (p > v && (p[-1] == ' ' || p[-1] == '\t'))
            --p;
        *valStart = v;
        *valLen = static_cast<size_t>(p - v);
        return true;
    }
    return false;
}

} // namespace AddOns::Toc
