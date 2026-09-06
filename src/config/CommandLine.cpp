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

#include "CommandLine.h"

#include <windows.h>

namespace Config::CommandLine {

namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Stops at `a`'s terminator rather than reading `len` bytes unconditionally, so
// a switch name at the very end of the command line cannot read past it.
bool EqualsIgnoreCase(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (a[i] == '\0' || Lower(a[i]) != Lower(b[i]))
            return false;
    }
    return true;
}

size_t Length(const char *s) {
    size_t n = 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

// Start of the value token for `-name`, or null when the switch is absent.
const char *FindValue(const char *name) {
    const char *cmdLine = ::GetCommandLineA();
    if (cmdLine == nullptr || name == nullptr)
        return nullptr;
    const size_t nameLen = Length(name);
    if (nameLen == 0)
        return nullptr;

    for (const char *p = cmdLine; *p != '\0'; ++p) {
        if (*p != '-' && *p != '/')
            continue;
        // Only at a token boundary, so a path containing the name cannot match.
        if (p != cmdLine && !IsSpace(p[-1]) && p[-1] != '"')
            continue;
        if (!EqualsIgnoreCase(p + 1, name, nameLen))
            continue;

        const char *after = p + 1 + nameLen;
        if (*after == '=') {
            ++after;
        } else if (IsSpace(*after)) {
            while (IsSpace(*after))
                ++after;
        } else {
            continue; // a longer switch that merely starts with `name`
        }
        return after;
    }
    return nullptr;
}

// Walks the token at `p`, honoring double quotes so a value containing spaces
// arrives whole. `emit` takes each character and returns whether it was kept;
// the walk stops as a failure the moment one is not.
template <typename Emit> bool ReadToken(const char *p, Emit emit) {
    const bool quoted = (*p == '"');
    if (quoted)
        ++p;
    size_t n = 0;
    while (*p != '\0' && (quoted ? (*p != '"') : !IsSpace(*p))) {
        if (!emit(*p++))
            return false;
        ++n;
    }
    return n > 0;
}

} // namespace

bool Value(const char *name, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    const char *p = FindValue(name);
    if (p == nullptr)
        return false;

    size_t n = 0;
    const bool ok = ReadToken(p, [&](char c) {
        if (n + 1 >= outSize)
            return false;
        out[n++] = c;
        return true;
    });
    out[n] = '\0';
    return ok;
}

bool Value(const char *name, std::string &out) {
    out.clear();
    const char *p = FindValue(name);
    if (p == nullptr)
        return false;
    return ReadToken(p, [&](char c) {
        out.push_back(c);
        return true;
    });
}

} // namespace Config::CommandLine
