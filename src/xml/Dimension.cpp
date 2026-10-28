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

// A dimension element with only `x`: `<Size x="18"/>`, `<Anchor point="LEFT"
// x="2"/>`.
//
// The engine's reader (FUN_XML_READ_DIMENSION) stores `x` and `y` from the
// node's attributes, then decides whether it found any by looking at the `y`
// attribute alone: with no `y` and no child element it reports "No x or y
// attributes in element" and answers 0, so the frame loader never applies the
// size it just read. 4.3.4's Dimension::Load answers true when either
// attribute is set. The hook makes that call for a node with `x`, no `y` and
// no child: the engine's reader runs as before, writing the converted `x` and
// a 0 `y`, with its warning sent to a sink that drops it, and the answer is 1.

#include "Game.h"
#include "Offsets.h"
#include "xml/FrameLoad.h"

namespace Xml::Dimension {

namespace {

using ReadDimension_t = int(__fastcall *)(const void *node, float *x, float *y, void *status);
using StatusAdd_t = void(__cdecl *)(void *status, int level, const char *fmt, ...);

ReadDimension_t g_orig = nullptr;

void __cdecl IgnoreStatus(void * /*status*/, int /*level*/, const char * /*fmt*/, ...) {}

// A status object the reader can report to: VTBL_XML_STATUS_ADD is the only
// slot it calls.
const StatusAdd_t kSilentVtable[Offsets::VTBL_XML_STATUS_ADD / sizeof(void *) + 1] = {
    nullptr, nullptr, nullptr, &IgnoreStatus};
static_assert(Offsets::VTBL_XML_STATUS_ADD / sizeof(void *) == 3, "the status Add slot");
struct SilentStatus {
    const StatusAdd_t *vtable = kSilentVtable;
};

bool HasValue(const char *s) { return s != nullptr && *s != '\0'; }

int __fastcall ReadDimension_h(const void *node, float *x, float *y, void *status) {
    if (node != nullptr && HasValue(Attr(node, "x")) && Attr(node, "y") == nullptr &&
        FirstChild(node) == nullptr) {
        SilentStatus silent;
        g_orig(node, x, y, &silent);
        return 1;
    }
    return g_orig(node, x, y, status);
}

const Game::HookAutoRegister _hook{Offsets::FUN_XML_READ_DIMENSION, reinterpret_cast<void *>(&ReadDimension_h),
                                   reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace Xml::Dimension
