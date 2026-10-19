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

namespace Chat::EventArgs {

// RAII scope naming the sender of the CHAT_MSG_* event fired inside it, for
// the event's arg12 and for `GetCurrentChatGUID()`. The engine fires the event
// synchronously inside the call the scope wraps. Restores the prior state on
// destruction rather than resetting it, so a chat event fired from another's
// handler leaves the outer message's sender intact when the inner one returns.
//
// `flags` is the message's chat tag ("AFK"/"DND"/"GM", or null). A sender
// counts only when it is a player and the message is not GM-tagged, as in
// 4.3.4. `takesLineID` is false only for text emotes, which 4.3.4 fires with
// lineID 0.
class SenderScope {
public:
    SenderScope(uint64_t senderGuid, const char *flags, bool takesLineID);
    ~SenderScope();
    SenderScope(const SenderScope &) = delete;
    SenderScope &operator=(const SenderScope &) = delete;

private:
    uint64_t prevGuid_;
    bool prevTakesLineID_;
};

} // namespace Chat::EventArgs
