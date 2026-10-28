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
#include <string>
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

bool RedirectCall(uintptr_t site, uintptr_t expected, const void *target) {
    const auto *code = reinterpret_cast<const uint8_t *>(site);
    if (code[0] != 0xE8)
        return false;
    const auto callee = [site](int32_t rel) { return site + 5 + static_cast<uintptr_t>(rel); };
    int32_t rel = 0;
    std::memcpy(&rel, code + 1, sizeof rel);
    const uintptr_t to = reinterpret_cast<uintptr_t>(target);
    if (callee(rel) == to)
        return true;
    if (callee(rel) != expected)
        return false;
    rel = static_cast<int32_t>(to - (site + 5));
    return PatchBytes(reinterpret_cast<uint8_t *>(site) + 1, &rel, sizeof rel);
}

bool ReplaceWithCall(uintptr_t site, const uint8_t *original, size_t len, const void *target) {
    uint8_t code[64];
    if (len < 5 || len > sizeof code)
        return false;
    code[0] = 0xE8;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (site + 5));
    std::memcpy(code + 1, &rel, sizeof rel);
    std::memset(code + 5, 0x90, len - 5);
    const void *at = reinterpret_cast<const void *>(site);
    if (std::memcmp(at, code, len) == 0)
        return true;
    if (std::memcmp(at, original, len) != 0)
        return false;
    return PatchBytes(reinterpret_cast<void *>(site), code, len);
}

void BoundedCopy(char *dst, const char *src, size_t n) {
    if (dst == nullptr || n == 0)
        return;
    size_t i = 0;
    for (; src != nullptr && src[i] != '\0' && i + 1 < n; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

void EnsureParentDirs(const char *relPath) {
    std::string accum;
    for (const char *p = relPath; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/') {
            if (!accum.empty())
                CreateDirectoryA(accum.c_str(), nullptr);
            accum.push_back('\\');
        } else {
            accum.push_back(*p);
        }
    }
}

} // namespace Common
