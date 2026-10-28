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

#include "frame/ParentChange.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::ParentChange {

namespace {

AutoSubscribe *g_head = nullptr;

using SetParent_t = void(__fastcall *)(void *frame, void *edx, void *parent);
SetParent_t g_origSetParent = nullptr;

void __fastcall SetParent_h(void *frame, void *edx, void *parent) {
    g_origSetParent(frame, edx, parent);
    for (const AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        s->fn(frame);
}

const Game::HookAutoRegister _hook{Offsets::FUN_FRAME_SET_PARENT, reinterpret_cast<void *>(&SetParent_h),
                                   reinterpret_cast<void **>(&g_origSetParent)};

} // namespace

AutoSubscribe::AutoSubscribe(Listener fn) : fn(fn), next(g_head) { g_head = this; }

} // namespace Frame::ParentChange
