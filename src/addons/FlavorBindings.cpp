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

#include "baselib/Ascii.h"
#include "storm/File.h"
#include "addons/FlavorBindings.h"
#include "addons/FlavorToc.h"
#include "addons/Toc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace AddOns::FlavorBindings {

namespace {


constexpr size_t NPOS = static_cast<size_t>(-1);

// If `path` is an addon Bindings read (`…\AddOns\<Name>\Bindings.xml`, CI),
// return the length of the prefix up to the `Bindings.xml` filename (so a
// flavor filename can be appended in its place); else NPOS.
size_t AddonBindingsPrefix(const char *path) {
    if (path == nullptr) return NPOS;
    const size_t len = std::strlen(path);
    const char *kFile = "bindings.xml"; // 12 chars, compared case-insensitively
    const size_t flen = 12;
    if (len < flen + 1) return NPOS;
    for (size_t i = 0; i < flen; ++i)
        if (Ascii::ToLower(path[len - flen + i]) != kFile[i]) return NPOS;
    const size_t prefixLen = len - flen;
    if (path[prefixLen - 1] != '\\' && path[prefixLen - 1] != '/')
        return NPOS; // "Bindings.xml" must be a whole path segment
    for (size_t i = 0; i + 8 <= len; ++i)
        if (Ascii::EqualCI(path + i, 8, "\\AddOns\\")) return prefixLen;
    return NPOS;
}

// Write `<prefix>Bindings<suffix>.xml` (e.g. `...\AddOns\Foo\Bindings-Turtle.xml`)
// into `out`. Returns false if it would not fit.
bool BuildFlavorPath(const char *path, size_t prefixLen, const char *suffix,
                     char *out, size_t outSize) {
    const size_t slen = std::strlen(suffix);
    if (prefixLen + (sizeof "Bindings" - 1) + slen + sizeof ".xml" > outSize)
        return false;
    char *p = out;
    std::memcpy(p, path, prefixLen);
    p += prefixLen;
    std::memcpy(p, "Bindings", sizeof "Bindings" - 1);
    p += sizeof "Bindings" - 1;
    std::memcpy(p, suffix, slen);
    p += slen;
    std::memcpy(p, ".xml", sizeof ".xml"); // incl. NUL
    return true;
}

} // namespace

bool TryExists(const char *path, int mode, FileExistsFn orig) {
    const size_t prefixLen = AddonBindingsPrefix(path);
    if (prefixLen == NPOS)
        return false;

    const char *order[FlavorToc::kMaxSuffixes];
    const int count = FlavorToc::Suffixes(order);
    char cand[512];
    for (int i = 0; i < count; ++i) {
        if (BuildFlavorPath(path, prefixLen, order[i], cand, sizeof cand) &&
            orig(cand, mode) != 0)
            return true;
    }
    return false;
}

bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn orig) {
    const size_t prefixLen = AddonBindingsPrefix(path);
    if (prefixLen == NPOS)
        return false;

    const char *order[FlavorToc::kMaxSuffixes];
    const int count = FlavorToc::Suffixes(order);
    char cand[512];
    for (int i = 0; i < count; ++i) {
        if (!BuildFlavorPath(path, prefixLen, order[i], cand, sizeof cand))
            continue;
        // Serve the flavor file in place of Bindings.xml (a selected flavor
        // wins over a plain Bindings.xml). No recursion: `orig` is the original
        // reader passed by the FUN_FILE_READ hook.
        if (orig(unused, cand, outBuf, outSize, extraBytes, flag1, flag2) != 0)
            return true;
    }
    return false; // no flavor variant — caller reads the base Bindings.xml
}

} // namespace AddOns::FlavorBindings
