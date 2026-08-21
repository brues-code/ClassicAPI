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

namespace AddOns::FlavorToc {

// Signature of the engine file reader FUN_FILE_READ (0x00648620), __stdcall
// (callee cleans 28 bytes / RET 0x1C). Same shape addons/Embedded.cpp uses.
using FileReadFn = int(__stdcall *)(int unused, const char *path, void **outBuf,
                                    size_t *outSize, size_t extraBytes,
                                    int flag1, int flag2);

// Modern-client multi-flavor TOC selection, backported.
//
// A modern addon that targets several game flavors from one folder ships
// `<Name>_Vanilla.toc` / `_Mainline.toc` / `_Cata.toc` / … and often NO plain
// `<Name>.toc`. Vanilla 1.12 only ever opens `<Name>\<Name>.toc`, so such an
// addon never registers — it is invisible, not merely mis-loaded.
//
// Called from the FUN_FILE_READ hook. If `path` is an addon base-TOC read
// (`…\AddOns\<Name>\<Name>.toc`), probe the vanilla-appropriate flavor TOCs in
// precedence order and, if one exists, serve it via `orig` (as if it were the
// base TOC) and return true. Order, most specific first:
//   `<Name>_Turtle.toc`   — ONLY on a Turtle-lineage client (Turtle::Detected),
//                           a ClassicAPI extension for Turtle's server content;
//   `<Name>_Vanilla.toc`  — WoW Classic vanilla;
//   `<Name>_Classic.toc`  — all classic expansions.
// This deliberately wins over an existing base `<Name>.toc`, mirroring the
// retail client (flavor-specific beats the generic base). We do NOT fall back
// to `_Mainline`/`_Cata`/etc.: an addon shipping only those does not support
// vanilla and should stay unlisted.
//
// The redirect fires at BOTH the registration scan (glue/char-select) and the
// in-world load pass, which must agree on the flavor. `Turtle::Detected()` is
// therefore consistent across both: it reads the in-world `TURTLE_WOW_VERSION`
// global AND Turtle's glue-time `TURTLE_*` GlueXML globals, so it returns true
// at the glue scan too (see turtle/Detect.h).
//
// Returns false when `path` is not a base-TOC read, or no vanilla flavor TOC
// exists — the caller then performs the normal base read. Because the redirect
// is at the read layer, the addon scan (FUN_0051C9B0), the load pass
// (FUN_006edb90) and GetAddOnMetadata all see the same selected TOC.
bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn orig);

} // namespace AddOns::FlavorToc
