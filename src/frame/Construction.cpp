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

// CSimpleFrame::CSimpleFrame is `__thiscall(frame, parent)`, `ret 4`, and
// returns the frame (benilla-re ui/frames; 16 callers, every frame factory
// and derived constructor). No other client DLL names its address.

#include "frame/Construction.h"

#include "Game.h"
#include "Offsets.h"

namespace Frame::Construction {

namespace {

using Constructor_t = void *(__fastcall *)(void *frame, void *edx, void *parent);
Constructor_t g_origConstructor = nullptr;

void *__fastcall Constructor_h(void *frame, void *edx, void *parent) {
    void *result = g_origConstructor(frame, edx, parent);
    for (const AutoSubscribe *s = AutoSubscribe::head; s != nullptr; s = s->next)
        s->fn(frame);
    return result;
}

const Game::HookAutoRegister _hook{Offsets::FUN_SIMPLEFRAME_CONSTRUCTOR,
                                   reinterpret_cast<void *>(&Constructor_h),
                                   reinterpret_cast<void **>(&g_origConstructor)};

} // namespace

} // namespace Frame::Construction
