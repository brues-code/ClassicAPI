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

#include "addons/FlavorToc.h"

#include "turtle/Detect.h"

#include <cstring>

namespace AddOns::FlavorToc {

// If `path` is `…\AddOns\<Name>\<Name>.toc` (case-insensitive), set `*prefixLen`
// to the byte length of the path up to and including the base `<Name>` filename
// (so a flavor suffix can be appended in place) and return true.
bool BaseTocPrefix(const char *path, size_t *prefixLen) {
    const size_t len = std::strlen(path);
    if (len < 5 || _strnicmp(path + len - 4, ".toc", 4) != 0)
        return false;

    // Last separator -> "<Name>.toc" file part.
    size_t lastSep = static_cast<size_t>(-1);
    for (size_t i = len; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { lastSep = i; break; }
    if (lastSep == static_cast<size_t>(-1))
        return false;
    const char *file = path + lastSep + 1;              // "<Name>.toc"
    const size_t nameLen = (len - (lastSep + 1)) - 4;   // strip ".toc"
    if (nameLen == 0)
        return false;

    // Previous separator -> "<Name>" folder segment; it must equal the base name.
    size_t prevSep = static_cast<size_t>(-1);
    for (size_t i = lastSep; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { prevSep = i; break; }
    if (prevSep == static_cast<size_t>(-1))
        return false;
    const char *folder = path + prevSep + 1;
    const size_t folderLen = lastSep - (prevSep + 1);
    if (folderLen != nameLen || _strnicmp(folder, file, nameLen) != 0)
        return false;

    // Grandparent segment must be "AddOns" (so FrameXML/other .toc reads pass through).
    size_t prev2Sep = static_cast<size_t>(-1);
    for (size_t i = prevSep; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { prev2Sep = i; break; }
    if (prev2Sep == static_cast<size_t>(-1))
        return false;
    const char *gp = path + prev2Sep + 1;
    const size_t gpLen = prevSep - (prev2Sep + 1);
    if (gpLen != 6 || _strnicmp(gp, "AddOns", 6) != 0)
        return false;

    *prefixLen = lastSep + 1 + nameLen; // "…\AddOns\<Name>\<Name>"
    return true;
}


int Suffixes(const char *(&out)[kMaxSuffixes]) {
    int count = 0;
    if (Turtle::Detected()) {
        out[count++] = "-Turtle";
        out[count++] = "_Turtle";
    }
    out[count++] = "-ClassicAPI";
    out[count++] = "_ClassicAPI";
    return count;
}

bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn orig) {
    if (path == nullptr)
        return false;

    size_t prefixLen = 0;
    if (!BaseTocPrefix(path, &prefixLen))
        return false;

    char cand[512];
    const char *suffixes[kMaxSuffixes];
    const int count = Suffixes(suffixes);
    for (int i = 0; i < count; ++i) {
        const size_t slen = std::strlen(suffixes[i]);
        if (prefixLen + slen + sizeof ".toc" > sizeof cand)
            return false; // pathological path length: let the base read run
        std::memcpy(cand, path, prefixLen);
        std::memcpy(cand + prefixLen, suffixes[i], slen);
        std::memcpy(cand + prefixLen + slen, ".toc", sizeof ".toc"); // incl. NUL
        if (orig(unused, cand, outBuf, outSize, extraBytes, flag1, flag2) != 0)
            return true; // served the flavor TOC in place of the base
    }
    // No flavor TOC: fall through. The caller reads the base path, which
    // chains through any other FUN_FILE_READ hooker before the engine.
    return false;
}

} // namespace AddOns::FlavorToc
