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

// Tracks in-progress addon loads so `C_AddOns.IsAddOnLoaded` can report
// `loadedOrLoading` and `loaded` separately. See `LoadState.h` for why the
// engine's own byte can't answer both.
//
// Loads nest — an addon's dependencies load inside its own call — so this
// keeps a stack rather than a single name. The name is copied rather than
// stored by pointer: `LoadAddOn` passes a Lua string, which does not
// outlive the call.

#include "addons/LoadState.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

namespace AddOns::LoadState {

namespace {

// Dependency chains are shallow; anything deeper simply isn't tracked
// (the depth counter still balances, so the stack can't desync).
constexpr int kMaxDepth = 16;
constexpr int kMaxName = 64;

char g_stack[kMaxDepth][kMaxName];
int g_depth = 0;

bool EqualsIgnoreCase(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }
    return *a == *b;
}

using LoadOne_t = unsigned int(__fastcall *)(const char *name, char flag,
                                             void *progressCtx);
LoadOne_t s_loadOne_o = nullptr;

unsigned int __fastcall LoadOne_h(const char *name, char flag, void *progressCtx) {
    const int slot = g_depth;
    if (slot >= 0 && slot < kMaxDepth)
        Common::BoundedCopy(g_stack[slot], name, kMaxName);
    ++g_depth;

    const unsigned int result = s_loadOne_o(name, flag, progressCtx);

    --g_depth;
    if (g_depth < 0)
        g_depth = 0; // defensive: never let the stack run negative
    return result;
}

const Game::HookAutoRegister _hookLoadOne{
    Offsets::FUN_ADDON_LOAD_ONE,
    reinterpret_cast<void *>(&LoadOne_h),
    reinterpret_cast<void **>(&s_loadOne_o)};

} // namespace

bool IsLoading(const char *name) {
    if (name == nullptr || *name == '\0')
        return false;
    const int depth = (g_depth < kMaxDepth) ? g_depth : kMaxDepth;
    for (int i = 0; i < depth; ++i)
        if (EqualsIgnoreCase(g_stack[i], name))
            return true;
    return false;
}

} // namespace AddOns::LoadState
