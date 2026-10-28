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

// `mixin="A, B"` on XML elements — see Mixin.cpp.

namespace Xml::Mixin {

// Mixes the mixins of `node`'s whole inherits chain into the Lua table at
// stack index `index`: each template's (recursively) in inherits order, then
// the node's own. `listInherits` reads `inherits` as a list, as frames do;
// regions and animation groups take it as one name. `owner` names the
// element in load warnings.
void ApplyChain(void *L, int index, const void *node, void *status, const char *owner,
                bool listInherits);

// For a loader hook that the engine re-enters for each inherited template:
// Begin before the engine loads `node`, End after. Only the object's
// outermost load applies the chain, so every mixin is in place before any
// node's KeyValues and scripts. `first`, when set, is a node whose chain
// applies before `node`'s: an intrinsic definition (Xml::Intrinsics).
void Begin(void *object, const void *node, void *status, bool listInherits,
           const void *first = nullptr);
void End(void *object);

} // namespace Xml::Mixin
