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

#include "Game.h"
#include "quest/Cache.h"

#include <cstdint>

namespace Quest::Title {

static const Game::Doc::Field kGetTitleForQuestIDArgs[] = {
    Game::Doc::Req("questID", "number"),
};
static const Game::Doc::Field kGetTitleForQuestIDRets[] = {
    Game::Doc::Opt("title", "cstring", nullptr, "Nil when the quest data is not loaded."),
};
static const Game::Doc::Function kGetTitleForQuestID{"Returns the title of a quest.",
                                                     kGetTitleForQuestIDArgs,
                                                     kGetTitleForQuestIDRets};

// `C_QuestLog.GetTitleForQuestID(questID)` — reads the title out of the
// engine's quest static-info cache. Returns nil if the data isn't loaded
// (callers should pair with `C_QuestLog.RequestLoadQuestByID` and listen
// for `QUEST_DATA_LOAD_RESULT`). Doesn't return header titles — modern
// WoW's same-named function explicitly excludes them and we mirror that
// (headers in 1.12 are stored in QuestSort.dbc, not in this cache).
//
// Title-offset derivation: traced from the engine's own quest-log title
// path at `0x004DF180`. After calling `_GetRecord` (which returns
// `entry+0x18`, the data block), the function does `add eax, 0x9C` and
// passes the result directly to `lua_pushstring` — i.e. the title is an
// inline null-terminated C string at offset `0x9C` within the data
// block. See [src/quest/Cache.h](src/quest/Cache.h) for the offset
// constant.
static int __fastcall Script_GetTitleForQuestID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_QuestLog.GetTitleForQuestID(questID)");
        return 0;
    }
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0)
        return 0;

    const uint8_t *data = Quest::Cache::Peek(static_cast<uint32_t>(questID));
    if (data == nullptr)
        return 0; // not in cache — caller should RequestLoadQuestByID and retry on event

    const char *title = reinterpret_cast<const char *>(data + Quest::Cache::OFF_TITLE);
    if (*title == '\0')
        return 0; // empty title (header / unknown record) → nil
    Game::Lua::PushString(L, title);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetTitleForQuestID",
                                     &Script_GetTitleForQuestID, &kGetTitleForQuestID);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Quest::Title
