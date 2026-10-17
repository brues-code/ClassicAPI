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

#include "script/LoadFile.h"

#include "Game.h"
#include "Offsets.h"

#include <intrin.h> // _ReturnAddress

namespace Script::LoadFile {

namespace {

// Chained at static-init time by the subscriber constructors, before `DllMain`.
// Zero-initialized (namespace static) ahead of any dynamic init.
AutoRewrite *g_rewriters = nullptr;
AutoSuppress *g_suppressors = nullptr;

using LoadFile_t = uint32_t(__fastcall *)(const char *path, void *ctx, void *status);
LoadFile_t g_orig = nullptr;

// Longer than any path the engine builds (its buffers are 0x104).
constexpr size_t kPathBuf = 0x208;

uint32_t __fastcall Hook(const char *path, void *ctx, void *status) {
    const uintptr_t retAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
    char buf[kPathBuf];
    for (AutoRewrite *r = g_rewriters; r != nullptr && path != nullptr; r = r->next)
        path = r->fn(path, retAddr, buf, sizeof buf);
    if (path != nullptr)
        for (AutoSuppress *s = g_suppressors; s != nullptr; s = s->next)
            if (s->fn(path))
                return 0;
    return g_orig(path, ctx, status);
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_LUA_LOAD_FILE, reinterpret_cast<void *>(&Hook),
    reinterpret_cast<void **>(&g_orig)};

} // namespace

AutoRewrite::AutoRewrite(Rewriter f) : fn(f), next(g_rewriters) { g_rewriters = this; }

AutoSuppress::AutoSuppress(Suppressor f) : fn(f), next(g_suppressors) {
    g_suppressors = this;
}

uint32_t Original(const char *path, void *ctx, void *status) {
    return g_orig != nullptr ? g_orig(path, ctx, status) : 0;
}

} // namespace Script::LoadFile
