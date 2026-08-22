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

#include "addons/EngineIO.h"
#include "addons/FlavorBindings.h"
#include "addons/Toc.h"

#include "Game.h"
#include "Offsets.h"
#include "turtle/Detect.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace AddOns::FlavorBindings {

namespace {

using AddOns::Toc::EqCI;
using AddOns::Toc::Lower;

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
        if (Lower(path[len - flen + i]) != kFile[i]) return NPOS;
    const size_t prefixLen = len - flen;
    if (path[prefixLen - 1] != '\\' && path[prefixLen - 1] != '/')
        return NPOS; // "Bindings.xml" must be a whole path segment
    for (size_t i = 0; i + 8 <= len; ++i)
        if (EqCI(path + i, 8, "\\AddOns\\")) return prefixLen;
    return NPOS;
}

// Write `<prefix><flavorFile>` (e.g. `…\AddOns\Foo\Bindings_Turtle.xml`) into
// `out`. Returns false if it would not fit.
bool BuildFlavorPath(const char *path, size_t prefixLen, const char *flavorFile,
                     char *out, size_t outSize) {
    const size_t flen = std::strlen(flavorFile);
    if (prefixLen + flen + 1 > outSize) return false;
    std::memcpy(out, path, prefixLen);
    std::memcpy(out + prefixLen, flavorFile, flen + 1); // incl. NUL
    return true;
}

// Fills `order` with the flavor filenames to probe, most specific first, and
// returns the count. `Bindings_Turtle.xml` only on a Turtle client.
int FlavorOrder(const char *(&order)[2]) {
    int count = 0;
    if (Turtle::Detected())
        order[count++] = "Bindings_Turtle.xml";
    order[count++] = "Bindings_ClassicAPI.xml";
    return count;
}

// Engine file-exists probe FUN_00648a30 — __stdcall(path, mode) (RET 8).
using EngineIO::FileExistsFn;
FileExistsFn g_origExists = nullptr;

// Co-hook the exists-check so `Interface\AddOns\<Name>\Bindings.xml` reports as
// existing when the base file is absent but a flavor variant is present — so
// the loader (`FUN_004b6f70`, then our TryHandle redirect) runs for a
// flavor-only addon. Every non-Bindings path passes straight through.
int __stdcall FileExists_h(const char *path, int mode) {
    const size_t prefixLen = AddonBindingsPrefix(path);
    if (prefixLen == NPOS)
        return g_origExists(path, mode);

    if (g_origExists(path, mode) != 0)
        return 1; // the plain Bindings.xml exists

    const char *order[2];
    const int count = FlavorOrder(order);
    char cand[512];
    for (int i = 0; i < count; ++i) {
        if (BuildFlavorPath(path, prefixLen, order[i], cand, sizeof cand) &&
            g_origExists(cand, mode) != 0)
            return 1;
    }
    return 0;
}

const Game::HookAutoRegister _hookExists{
    Offsets::FUN_FILE_EXISTS,
    reinterpret_cast<void *>(&FileExists_h),
    reinterpret_cast<void **>(&g_origExists)};

} // namespace

bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn orig) {
    const size_t prefixLen = AddonBindingsPrefix(path);
    if (prefixLen == NPOS)
        return false;

    const char *order[2];
    const int count = FlavorOrder(order);
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
