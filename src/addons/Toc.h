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

#include "baselib/Ascii.h"

#include <cstddef>
#include <cstring>

namespace AddOns::Toc {

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
            if (static_cast<size_t>(eol - p) > klen && Ascii::EqualCI(p, klen, key) &&
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

// Calls `fn(value, valueLen)` for every `## <key>:` line, in file order, with
// the same line rules as FindValue.
template <class Fn>
void ForEachValue(const char *buf, size_t size, const char *key, Fn &&fn) {
    const char *end = buf + size;
    const char *v = nullptr;
    size_t n = 0;
    while (buf < end && FindValue(buf, static_cast<size_t>(end - buf), key, &v, &n)) {
        fn(v, n);
        const char *eol = v + n;
        while (eol < end && *eol != '\r' && *eol != '\n')
            ++eol;
        buf = eol;
    }
}

// The 3.80.2 client's TOC true/false value (FUN_1405c85a0): a first character
// of 1-9, T, t, Y or y is true; 0, F, f, N or n is false; otherwise `on` and
// `enabled` (any case) are true and everything else is false.
inline bool Truthy(const char *v, size_t n) {
    if (n == 0)
        return false;
    const char c = v[0];
    if (c == '0' || c == 'F' || c == 'f' || c == 'N' || c == 'n')
        return false;
    if ((c >= '1' && c <= '9') || c == 'T' || c == 't' || c == 'Y' || c == 'y')
        return true;
    return Ascii::EqualCI(v, n, "on") || Ascii::EqualCI(v, n, "enabled");
}

// True when any `## <key>:` line is Truthy. 3.80.2 sets such flags only on a
// true value, so one true line keeps the flag set.
inline bool AnyTrue(const char *buf, size_t size, const char *key) {
    bool any = false;
    ForEachValue(buf, size, key, [&any](const char *v, size_t n) {
        if (Truthy(v, n))
            any = true;
    });
    return any;
}

} // namespace AddOns::Toc
