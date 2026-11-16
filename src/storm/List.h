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

#include <cstdint>

namespace Storm {

// A link in a Storm intrusive list (TSList, TSHashTable chains, the event and
// script handler lists) ends on a null pointer or on one with its low bit set,
// the list's own sentinel. True for a real node.
inline bool IsNode(uintptr_t link) { return link != 0 && (link & 1) == 0; }
inline bool IsNode(const void *link) { return IsNode(reinterpret_cast<uintptr_t>(link)); }

} // namespace Storm
