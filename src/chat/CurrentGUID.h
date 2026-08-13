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

namespace Chat::CurrentGUID {

// RAII scope that publishes the sender GUID of the chat message currently being
// dispatched, for `GetCurrentChatGUID()` to read. Chat::Dispatch constructs one
// around the original chat-dispatch call: the engine fires the CHAT_MSG_* event
// synchronously inside that call, so an addon's OnEvent sees the GUID of the
// message that triggered it. Restores the prior value on destruction rather than
// clearing to 0, so nested dispatch (a chat event firing during another's
// OnEvent — e.g. an addon calling SendChatMessage from its handler) leaves the
// outer context's GUID intact when the inner returns.
class DispatchScope {
public:
    DispatchScope(uint32_t guidLo, uint32_t guidHi);
    ~DispatchScope();
    DispatchScope(const DispatchScope &) = delete;
    DispatchScope &operator=(const DispatchScope &) = delete;

private:
    uint32_t prevLo_;
    uint32_t prevHi_;
};

} // namespace Chat::CurrentGUID
