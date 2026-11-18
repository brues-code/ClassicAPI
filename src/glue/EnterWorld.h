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

#pragma once

#include "AutoList.h"

#include <cstdint>

// `Glue::EnterWorld` — one co-hook on the glue's enter-world worker
// (FUN_GLUE_ENTER_WORLD), fanned out to subscribers. A MinHook target takes
// one hook, so every module that reacts to a world entry subscribes here
// instead of hooking the worker itself.
//
// Subscribers run after the original, only when it committed the entry (the
// login state is GLUE_STATE_ENTERING_WORLD; the rename / transfer-locked /
// not-connected paths return before that). `character` is the entered
// roster record (VAR_GLUE_SELECTED_CHAR, layout OFF_GLUE_CHAR_*), valid only
// during the call: the roster is freed once the world loads.
//
// Declare a file-scope
//     static const Glue::EnterWorld::AutoSubscribe _s{&OnEnterWorld};
namespace Glue::EnterWorld {

using Callback = void (*)(const uint8_t *character);
using AutoSubscribe = AutoList<struct AutoSubscribeTag, Callback>;

} // namespace Glue::EnterWorld
