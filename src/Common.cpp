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

#include "Common.h"

#include <cstring>
#include <windows.h>

namespace Common {

bool PatchBytes(void *dst, const void *src, size_t len) {
    DWORD oldProt = 0;
    if (!VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &oldProt))
        return false;

    std::memcpy(dst, src, len);
    FlushInstructionCache(GetCurrentProcess(), dst, len);

    DWORD dummy = 0;
    VirtualProtect(dst, len, oldProt, &dummy);
    return true;
}

void BoundedCopy(char *dst, const char *src, size_t n) {
    if (dst == nullptr || n == 0)
        return;
    size_t i = 0;
    for (; src != nullptr && src[i] != '\0' && i + 1 < n; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

} // namespace Common
