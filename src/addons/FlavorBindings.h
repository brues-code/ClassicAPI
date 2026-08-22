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

namespace AddOns::FlavorBindings {

using EngineIO::FileReadFn;

// Flavor-specific addon Bindings selection — the Bindings.xml analog of
// AddOns::FlavorToc.
//
// A modern addon can ship a flavor-specific keybinding file
// (`Bindings_Turtle.xml` / `Bindings_ClassicAPI.xml`) instead of / alongside a
// plain `Bindings.xml`. The per-addon loader `FUN_0051f240` only ever builds
// `Interface\AddOns\<Name>\Bindings.xml`, gates it on an exists-check
// (`FUN_00648a30`), and — when that passes — loads it via `FUN_004b6f70`, which
// reads the file through `FUN_FILE_READ`. Two pieces make flavor files work:
//
//   1. This TryHandle, called from the FUN_FILE_READ hook: when `path` is an
//      addon `…\AddOns\<Name>\Bindings.xml` read, serve a flavor variant in its
//      place if one exists (a selected flavor wins over a plain `Bindings.xml`,
//      matching FlavorToc). Probe order `Bindings_Turtle.xml` (only when
//      Turtle::Detected) then `Bindings_ClassicAPI.xml`.
//   2. A co-hook on the exists-check `FUN_00648a30` (inside FlavorBindings.cpp):
//      report `Bindings.xml` as existing when the base is absent but a flavor
//      variant is present — otherwise the loader is never called for a
//      flavor-only addon.
//
// Only `_Turtle` / `_ClassicAPI` are selected — never `_Vanilla` / `_Classic`
// (which target the 1.15 Classic Era build; its bindings reference handlers
// that build defines and this client does not load, so they would bind to
// missing functions). Both suffixes are ClassicAPI conventions, opt-in by the
// addon author, and mirror FlavorToc.
//
// Returns false when `path` is not an addon Bindings.xml read or no flavor
// variant exists — the caller then performs the normal base read.
bool TryHandle(int unused, const char *path, void **outBuf, size_t *outSize,
               size_t extraBytes, int flag1, int flag2, FileReadFn orig);

} // namespace AddOns::FlavorBindings
