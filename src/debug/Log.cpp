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

#include "Log.h"

#include "Game.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <windows.h> // SEH macros

namespace Debug::Log {

namespace {

// Resolved at first use to `<WoW.exe dir>\Logs\classicapi_debug.log`,
// matching where the engine drops `FrameXML.log` / `Sound.log` and
// where third-party DLLs like nampower drop their own logs. Lives
// next to the host process rather than in a fixed absolute path
// so the same DLL works against any client install. Cached after
// first resolution — `GetModuleFileNameA` is cheap but the path
// never changes once the DLL is loaded.
const char *LogPath() {
    static char cached[MAX_PATH] = {0};
    if (cached[0] != 0)
        return cached;

    char exePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0)
        return nullptr;

    if (char *slash = std::strrchr(exePath, '\\'))
        *slash = '\0'; // strip "WoW.exe", keep the directory

    char logsDir[MAX_PATH];
    std::snprintf(logsDir, sizeof(logsDir), "%s\\Logs", exePath);
    // Idempotent — no-op when the engine has already created Logs/
    // (the case for any client that's launched WoW at least once).
    CreateDirectoryA(logsDir, nullptr);

    std::snprintf(cached, sizeof(cached), "%s\\classicapi_debug.log",
                  logsDir);
    return cached;
}

// Open in append mode. Each writer call opens / writes / closes
// fresh — slow, but means a crash mid-trace still flushes what was
// written before the fault.
FILE *OpenAppend() {
    const char *path = LogPath();
    if (path == nullptr)
        return nullptr;
    FILE *f = nullptr;
    return fopen_s(&f, path, "a") == 0 ? f : nullptr;
}

// SEH-wrapped 16-byte-row read. Returns true if all 16 bytes
// copied successfully, false if the read faulted. Wrapping just
// the deref keeps the SEH frame tight — anything around it (file
// I/O, formatting) runs outside the protected region.
bool TryRead16(const uint8_t *src, uint8_t *dst) {
    __try {
        std::memcpy(dst, src, 16);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void HexDumpRow(FILE *f, uintptr_t addr, const uint8_t *bytes, int n) {
    char hex[64];
    char ascii[24];
    int hpos = 0, apos = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 8) {
            hex[hpos++] = ' ';
            ascii[apos++] = ' ';
        }
        if (i < n) {
            hpos += std::snprintf(hex + hpos, sizeof(hex) - hpos,
                                  "%02X ", bytes[i]);
            const uint8_t b = bytes[i];
            ascii[apos++] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        } else {
            hpos += std::snprintf(hex + hpos, sizeof(hex) - hpos, "   ");
            ascii[apos++] = ' ';
        }
    }
    hex[hpos] = '\0';
    ascii[apos] = '\0';
    std::fprintf(f, "0x%08X  %s |%s|\n", static_cast<unsigned>(addr), hex, ascii);
}

} // namespace

void Clear() {
    const char *path = LogPath();
    if (path == nullptr)
        return;
    FILE *f = nullptr;
    if (fopen_s(&f, path, "w") == 0 && f != nullptr)
        std::fclose(f);
}

void Append(const char *label, const char *content) {
    FILE *f = OpenAppend();
    if (f == nullptr)
        return;
    std::fprintf(f, "=== %s ===\n", label != nullptr ? label : "(no label)");
    std::fprintf(f, "%s\n\n", content != nullptr ? content : "");
    std::fclose(f);
}

void Printf(const char *fmt, ...) {
    if (fmt == nullptr)
        return;
    char line[2048];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n <= 0)
        return;

    FILE *f = OpenAppend();
    if (f == nullptr)
        return;
    std::fputs(line, f);
    // Auto-newline if the caller didn't include one.
    if (line[n - 1] != '\n')
        std::fputc('\n', f);
    std::fclose(f);
}

void HexDump(const char *label, const void *ptr, int len, uintptr_t base) {
    FILE *f = OpenAppend();
    if (f == nullptr)
        return;
    std::fprintf(f, "=== %s ===\n", label != nullptr ? label : "(no label)");

    if (ptr == nullptr || len <= 0) {
        std::fprintf(f, "(null/empty)\n\n");
        std::fclose(f);
        return;
    }

    auto *bytes = static_cast<const uint8_t *>(ptr);
    int off = 0;
    while (off < len) {
        uint8_t row[16] = {0};
        const int n = (len - off) < 16 ? (len - off) : 16;
        if (!TryRead16(bytes + off, row)) {
            std::fprintf(f, "0x%08X  <fault>\n",
                         static_cast<unsigned>(base + off));
            break;
        }
        HexDumpRow(f, base + off, row, n);
        off += 16;
    }
    std::fputc('\n', f);
    std::fclose(f);
}

void HexDump(const char *label, const void *ptr, int len) {
    HexDump(label, ptr, len, reinterpret_cast<uintptr_t>(ptr));
}

namespace {

int __fastcall Script_LogClear(void *) {
    Clear();
    return 0;
}

int __fastcall Script_LogAppend(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: _classicapi_LogAppend(label, content)");
        return 0;
    }
    const char *label = Game::Lua::ToString(L, 1);
    const char *content = Game::Lua::IsString(L, 2)
                              ? Game::Lua::ToString(L, 2)
                              : "";
    Append(label, content);
    return 0;
}

// `_classicapi_LogPrintf(line)` — appends `line` as a single
// trace entry. No formatting; the caller does string concat in Lua
// if needed. Provided so addons can drop one-liners into the same
// log without going through the labeled-block format.
int __fastcall Script_LogPrintf(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: _classicapi_LogPrintf(line)");
        return 0;
    }
    const char *line = Game::Lua::ToString(L, 1);
    if (line != nullptr)
        Printf("%s", line);
    return 0;
}

// `_classicapi_HexDump(label, addr, len)` — dumps `len` bytes
// starting at the numeric VA `addr`. Safe (SEH-wrapped) to point
// at any address — bad reads truncate the dump rather than
// crashing. Useful for inspecting struct layouts, looking at
// heap allocations the engine references via globals, etc.
int __fastcall Script_HexDump(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L,
            "Usage: _classicapi_HexDump(label, addr, len)");
        return 0;
    }
    const char *label = Game::Lua::ToString(L, 1);
    const uintptr_t addr = static_cast<uintptr_t>(
        Game::Lua::ToNumber(L, 2));
    const int len = static_cast<int>(Game::Lua::ToNumber(L, 3));
    HexDump(label, reinterpret_cast<const void *>(addr), len, addr);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_LogClear", &Script_LogClear, nullptr);
    Game::Lua::RegisterGlobalFunction("_classicapi_LogAppend", &Script_LogAppend, nullptr);
    Game::Lua::RegisterGlobalFunction("_classicapi_LogPrintf", &Script_LogPrintf, nullptr);
    Game::Lua::RegisterGlobalFunction("_classicapi_HexDump", &Script_HexDump, nullptr);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Debug::Log
