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

namespace AddOns::TocRewrite {

// Modern-client per-line TOC directives, backported.
//
// A single-TOC multi-flavor addon gates individual file lines with
// conditions and expands path variables:
//
//   [Family]\Init.lua                         # -> Classic\Init.lua
//   Vanilla.lua      [AllowLoadGameType vanilla]
//   Mainline.lua     [AllowLoadGameType mainline]   # dropped on this client
//   Localization\[TextLocale].lua             # -> Localization\enUS.lua
//   deDE.lua         [AllowLoadTextLocale deDE]
//
// Vanilla 1.12's TOC parsers never understood any of this. The scan
// parser (FUN_0051C9B0) reads only `##` metadata; the load pass
// (FUN_006edb90) hands each non-`#` line to the per-file loader verbatim,
// where the trailing ` [...]` breaks the extension check and the file
// silently never loads. `[Variable]` tokens likewise become part of a
// path that does not exist.
//
// This closes the gap at the FUN_FILE_READ layer (alongside FlavorToc /
// Embedded): after an addon TOC is read, rewrite its file-reference lines
// so the engine's own load loop sees clean paths. `##` metadata and `#`
// comment lines are copied verbatim, with ONE exception (`## Interface:`,
// below); metadata-line conditions, added in retail 12.0.7, are out of
// scope. What the tokens resolve to on this 1.12 client:
//
//   [AllowLoadGameType ...]   keep the line iff the list contains `vanilla`
//   [AllowLoadTextLocale ...] keep the line iff the list contains the
//                             client locale (GetLocale() code, e.g. enUS)
//   [AllowLoad ...]           addon files only load in-game, so `game`
//                             keeps and `glue` drops
//   [Family]                  -> Classic
//   [Game]                    -> Vanilla
//   [TextLocale]              -> the client locale code
//
// All conditions on a line must pass for it to load (AND). An unknown
// `[keyword args]` condition fails safe (drops the line); an unknown
// single-word `[Token]` is left verbatim (the engine then fails to find
// that path, so the file does not load).
//
// Multi-flavor `## Interface:` list. Retail lets a TOC list several
// interface versions (`## Interface: 120100, 50504, 11200`). Vanilla's
// parser is a plain atoi that reads only the FIRST number, so it never
// sees a later 11200 and marks the addon out of date. When the client
// version (FUN_ADDON_CLIENT_INTERFACE_VERSION, 11200) appears anywhere in
// the list but is not first, the `## Interface:` line is rewritten to that
// single value so the engine marks the addon compatible. A list WITHOUT
// the client version is left unchanged — it still parses to its first
// value and stays out of date, exactly as before.
//
// Called from the FUN_FILE_READ hook after the read succeeds. Does
// nothing unless `path` is an addon `.toc` read AND its content contains a
// `[` (directives) or a `,` (a possible `## Interface:` list) — the
// fast-path bail keeps every unaffected TOC zero-copy, and a rebuild that
// changes nothing is discarded by a content compare. When it does rewrite,
// it allocates a fresh Storm buffer, frees the original, and updates
// `*outBuf` / `*outSize` — the caller frees the replacement exactly as it
// would the engine's own read buffer.
void Transform(const char *path, void **outBuf, size_t *outSize);

} // namespace AddOns::TocRewrite
