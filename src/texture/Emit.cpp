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

#include "texture/Emit.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Texture::Emit {

namespace {

// __thiscall(texture, batch), ret 4; dummy-EDX form.
using Emit_t = void(__fastcall *)(void *texture, void *edx, void *batch);
Emit_t g_origEmit = nullptr;

void __fastcall Emit_h(void *texture, void *edx, void *batch) {
    const uint32_t before = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT);
    g_origEmit(texture, edx, batch);
    const bool appended = Game::Read<uint32_t>(batch, Offsets::OFF_RENDERBATCH_QUAD_COUNT) != before;
    for (const AutoSubscribe *s = AutoSubscribe::head; s != nullptr; s = s->next)
        s->fn(texture, batch, appended);
}

const Game::HookAutoRegister _hook{Offsets::FUN_SIMPLETEXTURE_EMIT, reinterpret_cast<void *>(&Emit_h),
                                   reinterpret_cast<void **>(&g_origEmit)};

} // namespace

} // namespace Texture::Emit
