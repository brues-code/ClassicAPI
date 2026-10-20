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

#include "frame/Destruction.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::Destruction {

namespace {

AutoSubscribe *g_head = nullptr;

using Destructor_t = void(__fastcall *)(void *frame);
Destructor_t g_origDestructor = nullptr;

void __fastcall Destructor_h(void *frame) {
    for (const AutoSubscribe *s = g_head; s != nullptr; s = s->next)
        s->fn(frame);
    g_origDestructor(frame);
}

const Game::HookAutoRegister _hook{Offsets::FUN_FRAME_DESTRUCTOR, reinterpret_cast<void *>(&Destructor_h),
                                   reinterpret_cast<void **>(&g_origDestructor)};

} // namespace

AutoSubscribe::AutoSubscribe(Listener fn) : fn(fn), next(g_head) { g_head = this; }

} // namespace Frame::Destruction
