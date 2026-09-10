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

#include <cstddef>

namespace Common {

bool PatchBytes(void *dst, const void *src, size_t len);

// Bounded, always-terminated string copy: writes at most `n - 1`
// characters plus the terminator, and treats a null `src` as empty. `n`
// is the size of `dst`. Stands in for `strncpy`, which MSVC deprecates
// (C4996) and which does not terminate on truncation.
void BoundedCopy(char *dst, const char *src, size_t n);

} // namespace Common
