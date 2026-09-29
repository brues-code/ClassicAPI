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

// `QUEST_QUERY_COMPLETE` - the event later clients fire once the completed-
// quest list requested by QueryQuestsCompleted() has arrived.
//
// Vanilla has neither the request nor the event, and its protocol has no
// completed-quests query. Where a server offers the list another way (Turtle
// WoW's `.queststatus`, answered over addon messages), the bundled addon's
// Util/QuestCompletion.lua gathers it; this module reserves the event name so
// frames can RegisterEvent it, and gives that Lua code
// `_classicapi_FireQuestQueryComplete()` to fire it when the list is in.

#include "Game.h"
#include "event/Custom.h"

namespace Quest::QueryComplete {

namespace {

const Event::Custom::AutoReserve _reserve{"QUEST_QUERY_COMPLETE"};

int __fastcall Script_FireQuestQueryComplete(void *L) {
    (void)L;
    Event::Custom::Fire(_reserve.Slot(), "");
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("_classicapi_FireQuestQueryComplete",
                                      &Script_FireQuestQueryComplete);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Quest::QueryComplete
