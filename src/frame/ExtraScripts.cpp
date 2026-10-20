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

#include "frame/ExtraScripts.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"

namespace Frame::ExtraScripts {

namespace {

// Zero-initialized before any Script's dynamic initializer runs.
Script *g_head = nullptr;

// The engine's __thiscall(frame, name) -> slot, modelled as __fastcall with a
// dummy edx so `name` lands on the first stack slot.
using Resolver_t = int(__fastcall *)(void *frame, void *edx, const char *name);
Resolver_t g_origResolver = nullptr;

int __fastcall Resolver_h(void *frame, void *edx, const char *name) {
    const int slot = g_origResolver(frame, edx, name);
    if (slot != 0) // a script the engine knows - leave it
        return slot;
    for (const Script *s = g_head; s != nullptr; s = s->next) {
        if (!Ascii::EqualCI(name, s->nameLower))
            continue;
        if (!Game::Lua::IsA(frame, s->typeIdVar)) // another class may have the name
            continue;
        Script::Cell &cell = s->cells[frame];
        return reinterpret_cast<int>(cell.v);
    }
    return 0;
}

const Game::HookAutoRegister _hook{Offsets::FUN_FRAME_SCRIPT_RESOLVER, reinterpret_cast<void *>(&Resolver_h),
                                   reinterpret_cast<void **>(&g_origResolver)};

void PrepareForReload() {
    for (Script *s = g_head; s != nullptr; s = s->next)
        s->cells.clear();
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

Script::Script(const char *nameLower, uintptr_t typeIdVar)
    : nameLower(nameLower), typeIdVar(typeIdVar), next(g_head) {
    g_head = this;
}

uint32_t *Script::Slot(const void *frame) const {
    const auto it = cells.find(frame);
    return it != cells.end() ? it->second.v : nullptr;
}

} // namespace Frame::ExtraScripts
