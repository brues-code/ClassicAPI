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

// `intrinsic="true"` frame types — see Intrinsics.cpp. The frame loader
// steps call in here at fixed points, so the definition is applied before
// everything else on a frame of that type.

namespace Xml::Intrinsics {

// The builder's step for a node it is handed: true when the node is an
// intrinsic definition, which is kept instead of built.
bool Define(const void *node, void *status);

// Keeps the node the builder is building for the intrinsic factory, which
// is handed only the parent.
class BuildScope {
public:
    explicit BuildScope(const void *node);
    ~BuildScope();
    BuildScope(const BuildScope &) = delete;
    BuildScope &operator=(const BuildScope &) = delete;
};

// The definition a node builds on: its tag's, else the first in its
// inherits chain, or nullptr. A second, different intrinsic in the chain is
// reported to `status` (which may be null) and ignored.
const void *ForChain(const void *node, void *status);

// The frame loader's step: on the outermost load of an object, loads the
// definition its node builds on first and binds the definition's scripts
// as intrinsics.
class LoadScope {
public:
    LoadScope(void *object, const void *node, void *status);
    ~LoadScope();
    LoadScope(const LoadScope &) = delete;
    LoadScope &operator=(const LoadScope &) = delete;
};

} // namespace Xml::Intrinsics
