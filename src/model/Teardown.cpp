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

#include "model/Teardown.h"

#include "Game.h"
#include "Offsets.h"

namespace Model::Teardown {
namespace {

using Destroy_t = void(__fastcall *)(void *frame);
Destroy_t g_origDestroy = nullptr;

void __fastcall Destroy_h(void *frame) {
    for (auto *node = AutoSubscribe::head; node != nullptr; node = node->next)
        node->fn(frame);
    g_origDestroy(frame);
}

const Game::HookAutoRegister _destroyHook{Offsets::FUN_SIMPLEMODEL_DESTROY,
                                          reinterpret_cast<void *>(&Destroy_h),
                                          reinterpret_cast<void **>(&g_origDestroy)};

} // namespace

} // namespace Model::Teardown
