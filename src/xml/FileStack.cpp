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

#include "xml/FileStack.h"

#include "Game.h"
#include "Offsets.h"

namespace Xml::FileStack {

namespace {

using XmlLoadFile_t = uintptr_t(__fastcall *)(const char *path, void *ctx, void *status);

XmlLoadFile_t g_origXmlLoadFile = nullptr;

// <Include> nesting is shallow; deeper levels still count so pops stay
// balanced, they just are not recorded.
constexpr int kMaxDepth = 16;
const char *g_files[kMaxDepth];
int g_depth = 0;

uintptr_t __fastcall XmlLoadFile_h(const char *path, void *ctx, void *status) {
    if (g_depth < kMaxDepth)
        g_files[g_depth] = path;
    ++g_depth;
    const uintptr_t r = g_origXmlLoadFile(path, ctx, status);
    --g_depth;
    return r;
}

const Game::HookAutoRegister _hook{Offsets::FUN_XML_LOAD_FILE,
                                   reinterpret_cast<void *>(&XmlLoadFile_h),
                                   reinterpret_cast<void **>(&g_origXmlLoadFile)};

} // namespace

const char *Current() {
    if (g_depth <= 0 || g_depth > kMaxDepth)
        return nullptr;
    return g_files[g_depth - 1];
}

int Depth() { return g_depth; }

} // namespace Xml::FileStack
