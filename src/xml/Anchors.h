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

// Later-client <Anchor> attributes — see Anchors.cpp.

namespace Xml::Anchors {

// Re-sets each <Anchor> of `node` that uses `relativeKey` or the `x` / `y`
// attributes, after the engine's loader has set the anchors it understands.
void Apply(void *object, const void *node, void *status);

// Retries the queued anchors of `root` and the regions under it; run once a
// frame's child <Frames> are built.
void RetryUnder(void *root, void *status);

// Retries every queued anchor and reports the ones that still do not resolve;
// run when the outermost frame build returns.
void Flush(void *status);

// The object a later-client key path names, starting from the region
// `object`, or nullptr: `$parent` steps to the region's parent, any other part
// indexes the current object's Lua table. With `leadingGlobal` (relativeKey),
// a first part that is not `$parent` is a global instead.
void *ResolveKeyPath(void *object, const char *path, bool leadingGlobal);

} // namespace Xml::Anchors
