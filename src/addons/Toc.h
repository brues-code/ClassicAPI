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

// Scans an in-memory TOC buffer for the `## <key>:` directive and returns the
// first one's value via out-params: [*valStart, *valStart + *valLen), trimmed
// of leading and trailing spaces/tabs and excluding the CR/LF/EOF terminator.
// `key` is the bare name ("Version", "SavedVariables"). Returns false
// (out-params untouched) when the directive is absent.
//
// Matches lines the way the engine's TOC parser `FUN_TOC_PARSER` (0x0051C9B0)
// does, so a directive the engine reads is never one we miss: a UTF-8 BOM at
// the start of the file is skipped; lines break at CR or LF; a line counts
// when it starts with "##", then any number of spaces, then `key`
// case-insensitively, IMMEDIATELY followed by ':' (the engine compares
// against literals like "SavedVariables:", which is also why
// "SavedVariables" never matches a "SavedVariablesPerCharacter:" line).
//
// Pure — reads no files; the caller owns the buffer (via FUN_FILE_READ, or an
// embedded copy). Shared by every addon module that reads a TOC directive so
// the line-scan lives in one place.
inline bool FindValue(const char *buf, size_t size, const char *key,
                      const char **valStart, size_t *valLen) {
    const size_t klen = std::strlen(key);
    if (klen == 0)
        return false;
    const char *end = buf + size;
    const char *line = buf;
    if (size >= 3 && static_cast<unsigned char>(buf[0]) == 0xEF &&
        static_cast<unsigned char>(buf[1]) == 0xBB &&
        static_cast<unsigned char>(buf[2]) == 0xBF)
        line += 3;
    while (line < end) {
        const char *eol = line;
        while (eol < end && *eol != '\r' && *eol != '\n')
            ++eol;
        const char *p = line;
        if (eol - p >= 2 && p[0] == '#' && p[1] == '#') {
            p += 2;
            while (p < eol && *p == ' ')
                ++p;
            if (static_cast<size_t>(eol - p) > klen && EqCI(p, klen, key) &&
                p[klen] == ':') {
                p += klen + 1;
                while (p < eol && (*p == ' ' || *p == '\t'))
                    ++p;
                const char *v = p;
                p = eol;
                while (p > v && (p[-1] == ' ' || p[-1] == '\t'))
                    --p;
                *valStart = v;
                *valLen = static_cast<size_t>(p - v);
                return true;
            }
        }
        line = eol + 1;
    }
    return false;
}

} // namespace AddOns::Toc
