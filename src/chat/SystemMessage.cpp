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

// `SendSystemMessage(msg)` — a local CHAT_MSG_SYSTEM line.
//
// 4.3.4 calls CGChat::AddChatMessage(msg, CHAT_MSG_SYSTEM, NULL). 1.12's
// equivalent is the chat line function FUN_CHAT_DISPATCH, called exactly as the
// game-message display FUN_00496720 calls it for a chat-kind message (0x00496822):
// chat type 10 in EDX and ten zero stack args. The engine copies the text into
// its own 0x800 buffer with a bounded SStrCopy (0x0049A9F1), so any length is
// safe. The call goes through Chat::Dispatch's hook like every engine system
// line does.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Chat::SystemMessage {

namespace {

constexpr int CHAT_MSG_SYSTEM = 0x0A;

using ChatDispatch_t = void(__fastcall *)(
    const char *message, int chatType,
    const char *senderName, int languageID, const char *channel, const char *targetName,
    const char *flags, int speakerGuidLo, int speakerGuidHi, int rank,
    int guidLo, int guidHi);

int __fastcall Script_SendSystemMessage(void *L) {
    if (!Game::Lua::IsString(L, 1))
        Game::Lua::Error(L, "Usage: SendSystemMessage(\"message\")");
    auto dispatch = reinterpret_cast<ChatDispatch_t>(
        static_cast<uintptr_t>(Offsets::FUN_CHAT_DISPATCH));
    dispatch(Game::Lua::ToString(L, 1), CHAT_MSG_SYSTEM, nullptr, 0, nullptr, nullptr,
             nullptr, 0, 0, 0, 0, 0);
    return 0;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("message", "string", "The text of the line."),
};
const Game::Doc::Function kSendSystemMessage{
    "Shows a line in chat as a system message, only on your own client. It fires "
    "CHAT_MSG_SYSTEM with the text as arg1.",
    kArgs, {}, "ChatGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SendSystemMessage", &Script_SendSystemMessage,
                                      &kSendSystemMessage);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Chat::SystemMessage
