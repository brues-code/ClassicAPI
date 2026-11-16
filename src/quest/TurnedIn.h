// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

#include <cstdint>

namespace Quest::TurnedIn {

// A turn-in the server confirmed (SMSG_QUESTGIVER_QUEST_COMPLETE), decoded
// once. Subscribers run BEFORE the engine's handler prints "<quest>
// completed." and the reward lines, so state they update is current for the
// CHAT_MSG_SYSTEM those lines fire; QUEST_TURNED_IN fires after them.
using Callback = void (*)(uint32_t questID);
struct AutoSubscribe {
    explicit AutoSubscribe(Callback fn);
    Callback fn;
    AutoSubscribe *next;
};

} // namespace Quest::TurnedIn
