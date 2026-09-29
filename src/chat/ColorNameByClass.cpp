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

// `UPDATE_CHAT_COLOR_NAME_BY_CLASS(chatType, colorNameByClass)` - the event
// later clients fire when SetChatColorNameByClass changes whether a chat
// type's sender names are drawn in their class color.
//
// The setting itself is the bundled addon's (Util/ChatColorNameByClass.lua:
// SetChatColorNameByClass, the per-character setting, the name coloring).
// This module reserves the event name so frames can RegisterEvent it, and
// gives that Lua `_classicapi_FireUpdateChatColorNameByClass(chatType, on)`.
// Later clients pass a boolean; the engine's event formatter has no boolean,
// so `colorNameByClass` arrives as "1" or nil (a `%s` of NULL pushes nil),
// which reads the same in any truth test.

#include "Game.h"
#include "event/Custom.h"

namespace Chat::ColorNameByClass {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("chatType", "string"),
    Game::Doc::Opt("colorNameByClass", "string", nullptr,
                   "\"1\" when sender names of the chat type show their class color."),
};
const Game::Doc::Event kDoc{
    "ChatInfo",
    "Fires when a chat type starts or stops showing sender names in their class color.",
    kPayload};
const Event::Custom::AutoReserve _reserve{"UPDATE_CHAT_COLOR_NAME_BY_CLASS", &kDoc};

int __fastcall Script_Fire(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const char *chatType = Game::Lua::ToString(L, 1);
    const bool on = Game::Lua::ToBoolean(L, 2) != 0;
    Event::Custom::Fire(_reserve.Slot(), "%s%s", chatType, on ? "1" : nullptr);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_FireUpdateChatColorNameByClass", &Script_Fire);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Chat::ColorNameByClass
