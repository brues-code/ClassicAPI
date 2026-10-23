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

// Per-object owner tags — 3.3.5's `GCheader::taint` + `lua_gctag`, the data
// behind `GetAddOnMemoryUsage` (and later `GetAddOnCPUUsage`).
//
// 3.3.5 stamps every new collectable object with
//     tag = (lua_tainted && lua_gctag) ? lua_gctag : lua_tainted
// in luaC_link / newlstr / luaS_newudata. 1.12 already maintains
// `lua_tainted` — the context word `VAR_FRAMESCRIPT_EXEC_CONTEXT`, which the
// addon loader sets to the addon's name pointer and script slots, constants
// and value copies carry — but its GC header has no room for a tag (a
// table's spare header bytes hold flags/lsizenode, a closure's isC/nupvalues),
// so the tag lives here, keyed by object, added on create and dropped on free
// through `Gc::Hooks`. Tags are compared by pointer and never dereferenced.
//
// `lua_gctag` is this module's pin stack: `PinScope` sets it for exactly the
// windows 3.3.5 sets it (an addon's load, a template's application).

namespace Gc::Tag {

// The tag of a live object, or nullptr.
const char *Of(const void *object);

// Bytes the object occupies, by the sizes the engine's own free functions
// pass to the allocator.
uint32_t SizeOf(const void *object);

// Number of live tagged objects.
size_t TaggedCount();

// `lua_gctag` for the scope's lifetime; nests.
void PushPin(const char *tag);
void PopPin();

struct PinScope {
    explicit PinScope(const char *tag) { PushPin(tag); }
    ~PinScope() { PopPin(); }
    PinScope(const PinScope &) = delete;
    PinScope &operator=(const PinScope &) = delete;
};

// `luaC_sizeall`: recount every tag's bytes from the live objects.
void UpdateUsage();

// `luaC_getsize`: a tag's bytes as of the last `UpdateUsage`; 0 if unseen.
uint64_t Usage(const char *tag);

} // namespace Gc::Tag
