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


// C_ChatInfo.SendChatMessage(message [, chatType [, languageID [, target]]]):
// the 3.80.2 client's only form (0x140B8ECD0; its global SendChatMessage is
// a Lua wrapper). Same order as 1.12's SendChatMessage, which this runs, so
// every engine check, filter and packet path stays the engine's. The one
// difference is the language: 3.80.2 takes a Languages.dbc id, 1.12 a
// localized name. An id with no Languages row sends nothing, as 3.80.2 sends
// nothing for one; nil leaves the race's language to the engine.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

namespace Chat::Send {

namespace {

int __fastcall Script_SendChatMessage(void *L) {
    if (Game::Lua::Type(L, 3) == Game::Lua::TYPE_NUMBER) {
        const double id = Game::Lua::ToNumber(L, 3);
        const char *name = id >= 0.0 ? DBC::LocalizedField(Offsets::VAR_LANGUAGES_RECORDS,
                                                           Offsets::VAR_LANGUAGES_COUNT, static_cast<uint32_t>(id),
                                                           Offsets::OFF_LANGUAGES_RECORD_NAME)
                                     : nullptr;
        if (name == nullptr)
            return 0;
        Game::Lua::PushString(L, name);
        Game::Lua::Replace(L, 3);
    }
    return reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_SEND_CHAT_MESSAGE)(L);
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("message", "cstring"),
    Game::Doc::Opt("chatType", "cstring", "SAY"),
    Game::Doc::Opt("languageID", "number", nullptr, "A Languages.dbc id; nil speaks the race's language."),
    Game::Doc::Opt("target", "cstring", nullptr, "The whisper target, or the channel number."),
};
const Game::Doc::Function kSendChatMessage{"Sends a chat message.", kArgs, {}};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_ChatInfo", "SendChatMessage", &Script_SendChatMessage, &kSendChatMessage);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Chat::Send
