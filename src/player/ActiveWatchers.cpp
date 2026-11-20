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

#include "player/ActiveWatchers.h"

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"

namespace Player::ActiveWatchers {

namespace {

using Register_t = void(__fastcall *)(int bank, uint32_t fieldOffset, uint32_t guidLo,
                                      uint32_t guidHi, int size, const void *callback,
                                      void *userArg1, void *userArg2);
using Unregister_t = void(__fastcall *)(int bank, uint32_t fieldOffset, uint32_t guidLo,
                                        uint32_t guidHi, const void *callback, void *userArg1);
using Watchers_t = void(__fastcall *)(void *player, void *edx);

Watchers_t g_origAdd = nullptr;
Watchers_t g_origRemove = nullptr;

// Add runs on arrival and on reactivation, after the engine's own.
void __fastcall AddActiveWatchers_h(void *player, void *edx) {
    g_origAdd(player, edx);
    const uint64_t guid = ::Object::Guid(player);
    for (AutoWatch *w = AutoWatch::head; w != nullptr; w = w->next)
        reinterpret_cast<Register_t>(Offsets::FUN_DESC_OBSERVER_REGISTER)(
            Offsets::DESC_OBSERVER_BANK_PLAYER, w->fn.fieldOffset, static_cast<uint32_t>(guid),
            static_cast<uint32_t>(guid >> 32), w->fn.size,
            reinterpret_cast<const void *>(w->fn.callback), nullptr, nullptr);
}

// Removed before the original, while the registry is as the engine's own
// removals find it.
void __fastcall RemoveActiveWatchers_h(void *player, void *edx) {
    const uint64_t guid = ::Object::Guid(player);
    for (AutoWatch *w = AutoWatch::head; w != nullptr; w = w->next)
        reinterpret_cast<Unregister_t>(Offsets::FUN_DESC_OBSERVER_UNREGISTER)(
            Offsets::DESC_OBSERVER_BANK_PLAYER, w->fn.fieldOffset, static_cast<uint32_t>(guid),
            static_cast<uint32_t>(guid >> 32), reinterpret_cast<const void *>(w->fn.callback),
            nullptr);
    g_origRemove(player, edx);
}

const Game::HookAutoRegister _addHook{Offsets::FUN_PLAYER_ADD_ACTIVE_WATCHERS,
                                      reinterpret_cast<void *>(&AddActiveWatchers_h),
                                      reinterpret_cast<void **>(&g_origAdd)};
const Game::HookAutoRegister _removeHook{Offsets::FUN_PLAYER_REMOVE_ACTIVE_WATCHERS,
                                         reinterpret_cast<void *>(&RemoveActiveWatchers_h),
                                         reinterpret_cast<void **>(&g_origRemove)};

} // namespace

} // namespace Player::ActiveWatchers
