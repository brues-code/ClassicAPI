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

// See CallerInterface.h. The stack walk uses the engine's own lua_getstack /
// lua_getinfo (Offsets::LUA_GET_STACK / LUA_GET_INFO, the pair luaL_where
// runs for error prefixes). The TOC is read through FUN_FILE_READ at the base
// `<Name>\<Name>.toc` path, so a flavor TOC the client selected is the one
// read (AddOns::FlavorToc redirects at that layer), and the embedded
// `!!!ClassicAPI` answers from its embedded TOC.

#include "api/CallerInterface.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"
#include "addons/Toc.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Api::CallerInterface {

namespace {

// Lua 5.0 `lua_Debug` (layout per Offsets::LUA_GET_STACK).
struct LuaDebug {
    int event;
    const char *name;
    const char *namewhat;
    const char *what;
    const char *source;
    int currentline;
    int nups;
    int linedefined;
    char short_src[60];
    int i_ci;
};
static_assert(sizeof(LuaDebug) == 0x60, "Lua 5.0 lua_Debug is 0x60 bytes");

using GetStack_t = int(__fastcall *)(void *L, int level, LuaDebug *ar);
using GetInfo_t = int(__fastcall *)(void *L, const char *what, LuaDebug *ar);
using AddOns::EngineIO::FileReadFn;
using AddOns::EngineIO::SMemFreeFn;

// How far to look past C frames (pcall, xpcall, ...) for the calling Lua
// function before giving up.
constexpr int kMaxLevels = 8;

struct Entry {
    std::string name; // the addon folder as chunk names spell it
    int iface;
};
std::vector<Entry> g_cache;

// The addon folder of a file chunk `@...\AddOns\<Name>\<file>` into `out`;
// false for any other chunk.
bool AddonFromSource(const char *source, char *out, size_t outSize) {
    if (source == nullptr || source[0] != '@')
        return false;
    for (const char *p = source + 1; *p != '\0'; ++p) {
        if (_strnicmp(p, "addons", 6) != 0 || (p[6] != '\\' && p[6] != '/'))
            continue;
        const char *name = p + 7;
        size_t n = 0;
        while (name[n] != '\0' && name[n] != '\\' && name[n] != '/')
            ++n;
        if (n == 0 || name[n] == '\0' || n + 1 > outSize)
            return false; // no file below the folder, or too long
        std::memcpy(out, name, n);
        out[n] = '\0';
        return true;
    }
    return false;
}

// The leading integer of the TOC's `## Interface:` line, 0 when absent.
int ReadInterface(const char *name) {
    char path[300];
    std::snprintf(path, sizeof path, "Interface\\AddOns\\%s\\%s.toc", name, name);

    void *buf = nullptr;
    size_t size = 0;
    auto fileRead = reinterpret_cast<FileReadFn>(Offsets::FUN_FILE_READ);
    if (fileRead(0, path, &buf, &size, 1, 1, 0) == 0 || buf == nullptr)
        return 0;

    int iface = 0;
    const char *v = nullptr;
    size_t n = 0;
    if (AddOns::Toc::FindValue(static_cast<const char *>(buf), size, "## Interface:",
                               &v, &n)) {
        for (size_t k = 0; k < n && v[k] >= '0' && v[k] <= '9'; ++k)
            iface = iface * 10 + (v[k] - '0');
    }

    auto smemFree = reinterpret_cast<SMemFreeFn>(Offsets::FUN_STORM_SMEM_FREE);
    smemFree(buf, __FILE__, __LINE__, 0);
    return iface;
}

// TOCs don't change within a session, so each addon's is read once.
int InterfaceFor(const char *name) {
    for (const Entry &e : g_cache)
        if (_stricmp(e.name.c_str(), name) == 0)
            return e.iface;
    const int iface = ReadInterface(name);
    g_cache.push_back({name, iface});
    return iface;
}

} // namespace

int Get(void *L) {
    auto getStack = reinterpret_cast<GetStack_t>(Offsets::LUA_GET_STACK);
    auto getInfo = reinterpret_cast<GetInfo_t>(Offsets::LUA_GET_INFO);
    LuaDebug ar{};
    for (int level = 1; level <= kMaxLevels; ++level) {
        if (getStack(L, level, &ar) == 0 || getInfo(L, "S", &ar) == 0)
            return 0;
        if (ar.what != nullptr && std::strcmp(ar.what, "C") == 0)
            continue;
        char name[128];
        return AddonFromSource(ar.source, name, sizeof name) ? InterfaceFor(name) : 0;
    }
    return 0;
}

} // namespace Api::CallerInterface
