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
#include <cstdint>

namespace Common {

bool PatchBytes(void *dst, const void *src, size_t len);

// Points the `E8 rel32` call at `site` to `target` when it still calls
// `expected`; true when the call now reaches `target`. A call that reaches
// anything else (another DLL's patch, a different build) is left alone.
// Idempotent, so it can run on every registration pass.
bool RedirectCall(uintptr_t site, uintptr_t expected, const void *target);

// RedirectCall for a `0F 8x rel32` conditional jump: points the taken branch
// at `site` to `target` when it still reaches `expected`. Same contract.
bool RedirectJcc(uintptr_t site, uintptr_t expected, const void *target);

// Writes `call target` plus NOP padding over the `len` bytes (5 to 64) of
// code at `site` when they still hold `original`; true when the site
// now calls `target`. A site holding anything else (another DLL's patch, a
// different build) is left alone. Idempotent, like RedirectCall.
bool ReplaceWithCall(uintptr_t site, const uint8_t *original, size_t len, const void *target);

// Bounded, always-terminated string copy: writes at most `n - 1`
// characters plus the terminator, and treats a null `src` as empty. `n`
// is the size of `dst`. Stands in for `strncpy`, which MSVC deprecates
// (C4996) and which does not terminate on truncation.
void BoundedCopy(char *dst, const char *src, size_t n);

// Creates every parent directory of `relPath` (backslash- or slash-separated,
// e.g. "BlizzardInterfaceCode\FrameXML\Foo.lua"), relative to the working
// directory. Directories that already exist are a no-op.
void EnsureParentDirs(const char *relPath);

} // namespace Common
