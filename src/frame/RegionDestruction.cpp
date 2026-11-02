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

#include "frame/RegionDestruction.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::RegionDestruction {

namespace {

AutoSubscribe *g_head = nullptr;

using Destruct_t = void(__fastcall *)(void *region);
Destruct_t g_origDestruct = nullptr;

void __fastcall Destruct_h(void *region) {
    for (const AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        s->fn(region);
    g_origDestruct(region);
}

const Game::HookAutoRegister _hook{Offsets::FUN_REGION_DESTRUCT, reinterpret_cast<void *>(&Destruct_h),
                                   reinterpret_cast<void **>(&g_origDestruct)};

} // namespace

AutoSubscribe::AutoSubscribe(Listener fn) : fn(fn), next(g_head) { g_head = this; }

} // namespace Frame::RegionDestruction
