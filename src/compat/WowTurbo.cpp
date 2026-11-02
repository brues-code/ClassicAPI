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

#include "compat/WowTurbo.h"

#include "Common.h"

#include <cstring>
#include <windows.h>

namespace Compat::WowTurbo {

namespace {

// The prologue patch wow_turbo's MinHook writes on x86: `jmp rel32` over the
// first five bytes. 1.12's entries open with `push ebp; mov ebp,esp`, never
// the `mov edi,edi` that would make MinHook hot-patch instead.
constexpr uint8_t kJmp = 0xE9;
constexpr size_t kJmpLen = 5;

// The `kJmpLen` bytes at `va` as WoW.exe holds them on disk, located through
// the loaded image's own section table.
bool ReadStock(uintptr_t va, uint8_t (&out)[kJmpLen]) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    const uintptr_t rva = va - base;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (rva < section->VirtualAddress ||
            rva + kJmpLen > section->VirtualAddress + section->SizeOfRawData)
            continue;
        wchar_t path[MAX_PATH];
        const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (len == 0 || len == MAX_PATH)
            return false;
        const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(rva - section->VirtualAddress + section->PointerToRawData);
        DWORD read = 0;
        const bool ok = ReadFile(file, out, kJmpLen, &read, &at) != 0 && read == kJmpLen;
        CloseHandle(file);
        return ok;
    }
    return false;
}

} // namespace

void Reclaim(uintptr_t function) {
    const HMODULE turbo = GetModuleHandleW(L"wow_turbo.dll");
    if (turbo == nullptr)
        return;
    const auto *entry = reinterpret_cast<const uint8_t *>(function);
    if (entry[0] != kJmp)
        return;
    int32_t rel = 0;
    std::memcpy(&rel, entry + 1, sizeof rel);
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(function + kJmpLen + rel), &owner) ||
        owner != turbo)
        return;
    uint8_t stock[kJmpLen];
    if (ReadStock(function, stock) && stock[0] != kJmp)
        Common::PatchBytes(reinterpret_cast<void *>(function), stock, kJmpLen);
}

} // namespace Compat::WowTurbo
