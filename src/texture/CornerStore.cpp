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

#include "texture/CornerStore.h"

#include "Game.h"
#include "Offsets.h"

namespace Texture::CornerStore {

namespace {

AutoSubscribe *g_head = nullptr;

// __thiscall(region, float rect[4]); dummy-EDX form.
using StoreCorners_t = void(__fastcall *)(void *region, void *edx, float *rect);
StoreCorners_t g_origStore = nullptr;

void __fastcall StoreCorners_h(void *region, void *edx, float *rect) {
    g_origStore(region, edx, rect);
    for (const AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        s->fn(region);
}

const Game::HookAutoRegister _hook{Offsets::FUN_REGION_STORE_CORNERS, reinterpret_cast<void *>(&StoreCorners_h),
                                   reinterpret_cast<void **>(&g_origStore)};

} // namespace

AutoSubscribe::AutoSubscribe(Listener fn) : fn(fn), next(g_head) { g_head = this; }

} // namespace Texture::CornerStore
