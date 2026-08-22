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

#include "addons/EngineIO.h"

namespace AddOns::FlavorToc {

using EngineIO::FileReadFn;

// Modern-client multi-flavor TOC selection, backported.
//
// A modern addon that targets several game flavors from one folder ships
// `<Name>_Vanilla.toc` / `_Mainline.toc` / `_Cata.toc` / … and often NO plain
// `<Name>.toc`. Vanilla 1.12 only ever opens `<Name>\<Name>.toc`, so such an
// addon never registers — it is invisible, not merely mis-loaded.
//
// Called from the FUN_FILE_READ hook. If `path` is an addon base-TOC read
// (`…\AddOns\<Name>\<Name>.toc`), probe the flavor TOCs that describe THIS
// environment and, if one exists, serve it via `orig` (as if it were the base
// TOC) and return true. Order, most specific first:
//   `<Name>_Turtle.toc`     — Turtle client/content, ONLY when Turtle is
//                             detected (Turtle::Detected); a ClassicAPI extension;
//   `<Name>_ClassicAPI.toc` — 1.12 + ClassicAPI, ALWAYS (ClassicAPI is present
//                             by definition — it is the code doing this redirect).
// Both are ClassicAPI conventions, not retail flavors. We deliberately do NOT
// map to `_Vanilla`/`_Classic` (those target the 1.15 Classic Era client — a
// modern engine + full modern API — which a 1.12 + partial-backport client is
// not), nor `_Mainline`/`_Cata`/etc. A selected flavor wins over an existing
// base `<Name>.toc`. If neither flavor exists, TryHandle returns false and the
// caller does the normal base read — which chains through any other DLL that
// hooked FUN_FILE_READ before reaching the engine.
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
