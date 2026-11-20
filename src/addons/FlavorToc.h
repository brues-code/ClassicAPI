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

#include "storm/File.h"

namespace AddOns::FlavorToc {

using FileReadFn = Storm::File::ReadFn;

// Modern-client multi-flavor TOC selection, backported.
//
// A modern addon that targets several game flavors from one folder ships
// `<Name>_Vanilla.toc` / `_Mainline.toc` / `_Cata.toc` / ... and often NO plain
// `<Name>.toc`. Vanilla 1.12 only ever opens `<Name>\<Name>.toc`, so such an
// addon never registers: it is invisible, not merely mis-loaded.
//
// The 3.80.2 client's rule (FUN_141af31b0 over the suffix list FUN_1401f50e0
// builds): try `<Name><suffix>.toc` for each suffix of the list, in order, and
// serve the first that exists; read the plain `<Name>.toc` only when none does.
// Its list is each flavor name with `-` and then `_` (`-Wrath`, `_Wrath`,
// `-WOTLKC`, `_WOTLKC`, `-Classic`, `_Classic`), most specific flavor first.
// Its Bindings file (`Bindings<suffix>.xml`) uses the same list.
//
// Our list (Suffixes) names only flavors that describe THIS environment:
//   `Turtle`     - Turtle client/content, ONLY when Turtle::Detected(); a
//                  ClassicAPI extension;
//   `ClassicAPI` - 1.12 + ClassicAPI, ALWAYS (ClassicAPI is present by
//                  definition: it is the code doing this redirect).
// Deliberately NOT `Vanilla`/`Classic` (those target the 1.15 Classic Era
// client, a modern engine + full modern API, which a 1.12 + partial-backport
// client is not), nor `Mainline`/`Cata`/etc. If no flavor file exists,
// TryHandle returns false and the caller does the normal base read, which
// chains through any other DLL that hooked FUN_FILE_READ before reaching the
// engine.
//
// The redirect fires at BOTH the registration scan (glue/char-select) and the
// in-world load pass, which must agree on the flavor. `Turtle::Detected()` is
// therefore consistent across both: it reads the in-world `TURTLE_WOW_VERSION`
// global AND Turtle's glue-time `TURTLE_*` GlueXML globals, so it returns true
// at the glue scan too (see turtle/Detect.h).

// If `path` is `...\AddOns\<Name>\<Name>.toc` (case-insensitive), sets
// `*prefixLen` to the length of the path up to and including the base
// `<Name>` file name (so a flavor suffix can be appended in place) and
// returns true.
bool BaseTocPrefix(const char *path, size_t *prefixLen);

// Room for every suffix Suffixes can return.
constexpr int kMaxSuffixes = 4;

// Fills `out` with the flavor suffixes to probe, in probe order, and returns
// the count: `-Turtle`, `_Turtle` (Turtle only), then `-ClassicAPI`,
// `_ClassicAPI`. Shared with AddOns::FlavorBindings.
int Suffixes(const char *(&out)[kMaxSuffixes]);

// Called from the FUN_FILE_READ hook. If `path` is an addon base-TOC read
// (`...\AddOns\<Name>\<Name>.toc`), serve the first flavor TOC of Suffixes
// that exists via `orig` (as if it were the base TOC) and return true.
// Returns false when `path` is not a base-TOC read, or no flavor TOC exists;
// the caller then performs the normal base read. Because the redirect is at
// the read layer, the addon scan (FUN_0051C9B0), the load pass (FUN_006edb90)
// and GetAddOnMetadata all see the same selected TOC.
bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn orig);

} // namespace AddOns::FlavorToc
