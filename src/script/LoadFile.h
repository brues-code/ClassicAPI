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
#include <cstdint>

// Shared interceptor for the engine's Lua file loader `FUN_LUA_LOAD_FILE`
// (0x00704BC0) — `__fastcall(const char *path, void *ctx, void *status)`. It
// runs every addon `.lua` (TOC lines and XML `<Script file>`) and every
// SavedVariables file.
//
// MinHook allows ONE hook per address, and more than one feature needs this
// loader: `Xml::ScriptPath` rewrites `<Script file>` paths, and
// `AddOns::SavedVarsFirst` skips the engine's redundant SavedVariables re-load.
// So — like `Frame::RunnerHook` — this module owns the single hook and fans
// out. Two subscriber kinds, applied in this order on every call:
//
//   1. `AutoRewrite` — may return a different path to load (written into
//      `buf`, which lives for the call). Each rewriter sees the path the one
//      before it returned. `retAddr` is the engine call site's return address,
//      so a rewriter can scope itself to one caller. `path` may already be
//      `buf` (an earlier rewriter's result); copy it out before writing `buf`.
//   2. `AutoSuppress` — sees the final path; `true` skips the load (returns 0).
//
// Rewriters must touch disjoint call sites and suppressors disjoint paths, so
// static-init order within a kind does not matter.

namespace Script::LoadFile {

using Rewriter = const char *(*)(const char *path, uintptr_t retAddr, char *buf,
                                 size_t bufSize);
using Suppressor = bool (*)(const char *path);

struct AutoRewrite {
    explicit AutoRewrite(Rewriter fn);
    Rewriter fn;
    AutoRewrite *next;
};

struct AutoSuppress {
    explicit AutoSuppress(Suppressor fn);
    Suppressor fn;
    AutoSuppress *next;
};

// The engine's loader (the MinHook trampoline), bypassing every subscriber.
// Returns 0 before hooks are installed.
uint32_t Original(const char *path, void *ctx, void *status);

} // namespace Script::LoadFile
