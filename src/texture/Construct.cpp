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

#include "texture/Construct.h"

#include "Game.h"
#include "Offsets.h"

namespace Texture::Construct {

namespace {

AutoSubscribe *g_head = nullptr;

// __thiscall(mem, parent, layer, sublayer) -> region; dummy-EDX __fastcall.
using Ctor_t = void *(__fastcall *)(void *mem, void *edx, void *parent, int layer, int sublayer);
Ctor_t g_origCtor = nullptr;

void *__fastcall Ctor_h(void *mem, void *edx, void *parent, int layer, int sublayer) {
    if (mem != nullptr)
        for (const AutoSubscribe *s = g_head; s != nullptr; s = s->next)
            s->fn(mem);
    return g_origCtor(mem, edx, parent, layer, sublayer);
}

const Game::HookAutoRegister _hook{Offsets::FUN_SIMPLETEXTURE_CTOR, reinterpret_cast<void *>(&Ctor_h),
                                   reinterpret_cast<void **>(&g_origCtor)};

} // namespace

AutoSubscribe::AutoSubscribe(Listener fn) : fn(fn), next(g_head) { g_head = this; }

} // namespace Texture::Construct
