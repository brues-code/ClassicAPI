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

#include <cstdint>

namespace AutoComplete::Names {

// Chat::Dispatch calls this for every dispatched chat line, before the engine
// fires its CHAT_MSG_* event. A whisper to or from a player (`guid` is the
// other player: Chat::Dispatch's sender) makes that player an InteractedWith
// name, most recent first — 4.3.4's ChatFrame does the same in its message
// handler. Every other line is ignored.
void OnChat(int chatType, int languageID, const char *name, uint64_t guid);

} // namespace AutoComplete::Names
