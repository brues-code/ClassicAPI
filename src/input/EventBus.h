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

// The engine's input event bus (key, mouse and activation events, the
// EVENT_ID_* ids), which the console and the frame system register on.

#include "Offsets.h"

namespace Input::EventBus {

// A handler: `data` is the event's payload. Returning 0 stops the dispatch.
using Handler = int(__fastcall *)(const void *data, void *param);

// EventRegisterEx (FUN_EVENT_REGISTER_EX, `__fastcall(id, handler, param,
// priority)`), into the current event context. Handlers run in descending
// priority, and a later handler at an equal priority runs first. Does not
// dedup, so a caller registers once.
inline void Register(int eventId, Handler handler, void *param, float priority) {
    using Fn = void(__fastcall *)(int, Handler, void *, float);
    reinterpret_cast<Fn>(Offsets::FUN_EVENT_REGISTER_EX)(eventId, handler, param, priority);
}

} // namespace Input::EventBus
