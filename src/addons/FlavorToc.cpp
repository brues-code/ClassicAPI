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

namespace {

// If `path` is `…\AddOns\<Name>\<Name>.toc` (case-insensitive), set `*prefixLen`
// to the byte length of the path up to and including the base `<Name>` filename
// (so a `_Vanilla.toc` suffix can be appended in place) and return true.
bool ParseBaseToc(const char *path, size_t *prefixLen) {
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

} // namespace

bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn orig) {
    if (path == nullptr)
        return false;

    size_t prefixLen = 0;
    if (!ParseBaseToc(path, &prefixLen))
        return false;

    char cand[512];
    if (prefixLen + 16 >= sizeof cand) // "_Vanilla.toc" + NUL fits in 16
        return false;                  // pathological path length — let base read run
    std::memcpy(cand, path, prefixLen);

    // Probe order, most specific first. `_Turtle` is offered only on a
    // Turtle-lineage client (gated on Turtle::Detected) so stock 1.12 never
    // prefers Turtle-only code; then the generic vanilla/classic fallbacks.
    // Deliberately NOT _Mainline/_Cata/etc — those are not for a vanilla client.
    const char *order[3];
    int count = 0;
    if (Turtle::Detected())
        order[count++] = "_Turtle.toc";
    order[count++] = "_Vanilla.toc";
    order[count++] = "_Classic.toc";

    for (int i = 0; i < count; ++i) {
        std::memcpy(cand + prefixLen, order[i], std::strlen(order[i]) + 1); // incl. NUL
        if (orig(unused, cand, outBuf, outSize, extraBytes, flag1, flag2) != 0)
            return true; // served the flavor TOC in place of the base
    }
    return false; // no matching flavor TOC — caller reads the base path
}

} // namespace AddOns::FlavorToc
